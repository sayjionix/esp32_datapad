#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Keypad.h>
#include <Preferences.h>
#include <LiquidCrystal.h> // Standard-Bibliothek für HD44780

// WLAN Zugangsdaten
const char* ssid = "FRITZ!Box 7490";
const char* password = "98865512128174437592";

// Jira Konfiguration
const char* jiraHost = "txteaviation.atlassian.net"; 
const char* base64Credentials = "cC5sYXVmZnNAYW1hemlsaWEuYWVybzpBVEFUVDN4RmZHRjAxWVYzelhmSXJQcXRRQmVzWXFJOGRyeXNnTlo3X2JIVEthNGZhZnBPRUJwcXRMMmxXMm1MWHdFYkZObUpsaWZGWGpkRVJVbGNkV055TzZvLVk4QlhHMFJOcVk3T3A5a0JRVzc3RjBMeExGSjJZTzd4cHV3UHZ5ZlpYMzVhczVQVFJaN2xNNXhiMlFtZ0lvV0pSUkY5Y0UxY3I0cVZ4NmtxbzhMMTZJMzk3SDg9OUJEOTVFQjY"; 

Preferences preferences;

// HD44780 LCD Pin-Zuweisung: LiquidCrystal lcd(rs, en, d4, d5, d6, d7)
LiquidCrystal lcd(22, 21, 19, 18, 5, 17); 

// Pin für die Steuerung der Hintergrundbeleuchtung
const int BACKLIGHT_PIN = 23; 

// Display-Timeout Variablen
unsigned long lastActivityTime = 0;
const unsigned long displayTimeout = 30000; // 30 Sekunden in ms
bool isDisplayOn = true;

// Keypad Definition (Pins angepasst, um Konflikte zu vermeiden)
const byte ROWS = 5; 
const byte COLS = 4; 
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','4'},
  {'5','6','7','8'},
  {'9','A','B','C'},
  {'D','E','F','G'},
  {'H','I','J','K'} 
};
byte rowPins[ROWS] = {13, 12, 14, 27, 26}; 
byte colPins[COLS] = {25, 33, 32, 4}; // Spalte 4 liegt jetzt auf GPIO 4

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

String taskMapping[20];
unsigned long startTimes[20] = {0};
int activeTimerIndex = -1; 

int configIndex = -1;      
int manualTimeIndex = -1;  

String currentInput = "";    
char lastT9Key = '\0';       
int t9CycleIndex = 0;        
unsigned long lastT9Time = 0;
const unsigned long t9Timeout = 1200; 

const String t9Chars[11] = {
  "1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9", "0 ", "-"
};

int getKeyIndex(char key) {
  if (key >= '1' && key <= '9') return key - '1';
  if (key >= 'A' && key <= 'G') return 9 + (key - 'A');
  if (key >= 'H' && key <= 'K') return 16 + (key - 'H');
  return -1;
}

int getT9LayoutIndex(char key) {
  if (key >= '1' && key <= '9') return key - '1';
  if (key == 'A') return 9;
  if (key == 'B') return 10;
  return -1;
}

// Schaltet die Hintergrundbeleuchtung ein und reaktiviert die Anzeige
void resetDisplayTimeout() {
  lastActivityTime = millis();
  if (!isDisplayOn) {
    digitalWrite(BACKLIGHT_PIN, HIGH); // Licht an
    isDisplayOn = true;
    updateDefaultDisplay(); 
  }
}

void updateDefaultDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Jira Tracker v1  ");
  lcd.setCursor(0, 1);
  lcd.print("--------------------");
  
  lcd.setCursor(0, 2);
  if (activeTimerIndex != -1) {
    lcd.print("Aktiv: " + taskMapping[activeTimerIndex]);
  } else {
    lcd.print("Status: Bereit.");
  }
  
  lcd.setCursor(0, 3);
  lcd.print("Klick=Start Hlt=Zeit");
}

void setup() {
  Serial.begin(115200);
  
  // Pin für Hintergrundbeleuchtung konfigurieren und einschalten
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  // LCD mit Dimensionen initialisieren (20 Spalten, 4 Zeilen)
  lcd.begin(20, 4);
  lcd.setCursor(0, 0);
  lcd.print("Booting System...");

  preferences.begin("jira_tasks", false);
  for (int i = 0; i < 20; i++) {
    String keyName = "task_" + String(i);
    taskMapping[i] = preferences.getString(keyName.c_str(), "PROJ-" + String(i + 1));
  }

  customKeypad.setHoldTime(2000); 

  lcd.setCursor(0, 1);
  lcd.print("WLAN verbinden...   ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  resetDisplayTimeout();
  updateDefaultDisplay();
}

void loop() {
  char key = customKeypad.getKey();
  KeyState kpadState = customKeypad.getState();

  // Automatische Display-Abschaltung nach 30 Sekunden Inaktivität
  if (isDisplayOn && (millis() - lastActivityTime > displayTimeout)) {
    lcd.clear();
    digitalWrite(BACKLIGHT_PIN, LOW); // Hintergrundbeleuchtung aus
    isDisplayOn = false;
    Serial.println("[Display] Gehe in den Energiesparmodus...");
  }

  // T9 Timeout für Buchstaben-Fixierung
  if (configIndex != -1 && currentInput.length() > 0 && (millis() - lastT9Time > t9Timeout) && lastT9Key != '\0') {
    lastT9Key = '\0'; 
    lcd.setCursor(9, 2);
    lcd.print(currentInput + " ");
  }

  // Prüfen ob 'K' gedrückt ist (Shift)
  bool isShiftPressed = false;
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      if (customKeypad.key[i].kchar == 'K' && 
         (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) {
        isShiftPressed = true;
      }
    }
  }

  if (key) {
    resetDisplayTimeout(); 
    int idx = getKeyIndex(key);

    if (configIndex != -1) {
      handleT9Input(key);
    } 
    else if (manualTimeIndex != -1) {
      handleManualTimeInput(key);
    } 
    else {
      // COMBO: T9 Setup starten
      if (isShiftPressed && key != 'K' && kpadState == PRESSED && idx != -1) {
        configIndex = idx;
        currentInput = "";
        lastT9Key = '\0';
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("!!! T9-SETUP !!!");
        lcd.setCursor(0, 1);
        lcd.print("Taste " + String(key) + " -> Jira Key");
        lcd.setCursor(0, 2);
        lcd.print("Eingabe: ");
        lcd.setCursor(0, 3);
        lcd.print("D=Del F=Save");
      } 
      // NORMALER KLICK: Start / Stopp
      else if (!isShiftPressed && kpadState == PRESSED && idx != -1 && key != 'K') {
        handleTracking(idx);
      }
    }
  }

  // LANGE HALTEN: Direkte Zeiteingabe
  if (kpadState == HOLD && configIndex == -1 && manualTimeIndex == -1) {
    for (int i = 0; i < ROWS; i++) {
      if (customKeypad.key[i].kstate == HOLD) {
        char heldKey = customKeypad.key[i].kchar;
        int idx = getKeyIndex(heldKey);
        if (idx != -1 && heldKey != 'K' && heldKey != 'D' && heldKey != 'F') {
          resetDisplayTimeout();
          manualTimeIndex = idx;
          currentInput = "";
          
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("MANUELLE ZEIT FUER:");
          lcd.setCursor(0, 1);
          lcd.print(taskMapping[manualTimeIndex]);
          lcd.setCursor(0, 2);
          lcd.print("Minuten: ");
          lcd.setCursor(0, 3);
          lcd.print("D=Del F=Buchen");
        }
      }
    }
  }

  // Sekunden-Update bei laufendem Timer
  if (isDisplayOn && activeTimerIndex != -1 && configIndex == -1 && manualTimeIndex == -1) {
    static unsigned long lastSecUpdate = 0;
    if (millis() - lastSecUpdate > 1000) {
      lastSecUpdate = millis();
      unsigned long totalSecs = (millis() - startTimes[activeTimerIndex]) / 1000;
      unsigned long mins = totalSecs / 60;
      unsigned long secs = totalSecs % 60;
      
      lcd.setCursor(0, 3);
      char timeBuf[21];
      sprintf(timeBuf, "Laufzeit: %02lu:%02lu      ", mins, secs);
      lcd.print(timeBuf);
    }
  }

  delay(10);
}

void handleManualTimeInput(char key) {
  if (key == 'F') {
    lcd.clear();
    if (currentInput.length() > 0) {
      long minutes = currentInput.toInt();
      if (minutes > 0) {
        lcd.setCursor(0, 0); lcd.print("Sende an Jira...");
        lcd.setCursor(0, 1); lcd.print(taskMapping[manualTimeIndex] + ": " + String(minutes) + "m");
        if (WiFi.status() == WL_CONNECTED) {
          logTimeToJira(taskMapping[manualTimeIndex].c_str(), minutes);
        } else {
          lcd.setCursor(0, 2); lcd.print("WLAN FEHLER!");
          delay(2000);
        }
      }
    }
    manualTimeIndex = -1;
    updateDefaultDisplay();
    return;
  }

  if (key == 'D') {
    if (currentInput.length() > 0) {
      currentInput.remove(currentInput.length() - 1);
      lcd.setCursor(9, 2); lcd.print(currentInput + "    ");
    }
    return;
  }

  if ((key >= '1' && key <= '9') || key == 'A') {
    char digit = (key == 'A') ? '0' : key;
    currentInput += digit;
    lcd.setCursor(9, 2); lcd.print(currentInput);
  }
}

void handleT9Input(char key) {
  if (key == 'F') {
    if (currentInput.length() > 0) {
      taskMapping[configIndex] = currentInput;
      String keyName = "task_" + String(configIndex);
      preferences.putString(keyName.c_str(), currentInput);
      lcd.clear(); lcd.setCursor(0, 1); lcd.print("Gespeichert!");
      delay(1500);
    }
    configIndex = -1;
    updateDefaultDisplay();
    return;
  }

  if (key == 'D') {
    if (currentInput.length() > 0) {
      currentInput.remove(currentInput.length() - 1);
      lastT9Key = '\0';
      lcd.setCursor(9, 2); lcd.print(currentInput + "    ");
    }
    return;
  }

  int t9Idx = getT9LayoutIndex(key);
  if (t9Idx != -1) {
    unsigned long now = millis();
    String chars = t9Chars[t9Idx];

    if (key == lastT9Key && (now - lastT9Time < t9Timeout)) {
      t9CycleIndex = (t9CycleIndex + 1) % chars.length();
      currentInput.setCharAt(currentInput.length() - 1, chars[t9CycleIndex]);
    } else {
      t9CycleIndex = 0;
      currentInput += chars[t9CycleIndex];
    }
    lastT9Key = key;
    lastT9Time = now;
    lcd.setCursor(9, 2); lcd.print(currentInput);
  }
}

void handleTracking(int index) {
  if (startTimes[index] != 0) {
    unsigned long durationMinutes = (millis() - startTimes[index]) / 60000;
    if (durationMinutes == 0) durationMinutes = 1;
    
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Stoppe & buche:");
    lcd.setCursor(0, 1); lcd.print(taskMapping[index] + " (" + String(durationMinutes) + "m)");
    
    if (WiFi.status() == WL_CONNECTED) {
      logTimeToJira(taskMapping[index].c_str(), durationMinutes);
    } else {
      lcd.setCursor(0, 2); lcd.print("WLAN FEHLER!");
      delay(2000);
    }
    startTimes[index] = 0;
    activeTimerIndex = -1;
    updateDefaultDisplay();
  } 
  else {
    if (activeTimerIndex != -1) {
      unsigned long oldDurationMinutes = (millis() - startTimes[activeTimerIndex]) / 60000;
      if (oldDurationMinutes == 0) oldDurationMinutes = 1;
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Auto-Stop Task:");
      lcd.setCursor(0, 1); lcd.print(taskMapping[activeTimerIndex]);
      
      if (WiFi.status() == WL_CONNECTED) {
        logTimeToJira(taskMapping[activeTimerIndex].c_str(), oldDurationMinutes);
      }
      startTimes[activeTimerIndex] = 0;
    }
    
    startTimes[index] = millis();
    activeTimerIndex = index;
    updateDefaultDisplay();
  }
}

void logTimeToJira(const char* issueKey, unsigned long minutes) {
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  String url = "https://" + String(jiraHost) + "/rest/api/3/issue/" + String(issueKey) + "/worklog";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + String(base64Credentials));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  String jsonPayload = "{\"timeSpent\": \"";
  jsonPayload += String(minutes);
  jsonPayload += "m\", \"comment\": {\"type\": \"doc\", \"version\": 1, \"content\": [{\"type\": \"paragraph\", \"content\": [{\"text\": \"Gebucht via ESP32 HD44780 Station.\", \"type\": \"text\"}]}]}}";
  
  int httpResponseCode = http.POST(jsonPayload);
  
  lcd.setCursor(0, 2);
  if (httpResponseCode == 201) {
    lcd.print("Jira: ERFOLG!       ");
  } else {
    lcd.print("Err HTTP:" + String(httpResponseCode));
  }
  delay(1500); 
  http.end();
}