#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Keypad.h>
#include <Preferences.h>
#include <LiquidCrystal.h>
#include <ArduinoJson.h>

Preferences preferences;

// HD44780 LCD Pins: LiquidCrystal lcd(rs, en, d4, d5, d6, d7)
LiquidCrystal lcd(9, 46, 8, 16, 15, 7);
const int BACKLIGHT_PIN = 4;

// Dynamische Variablen für Zugangsdaten (werden aus Preferences geladen)
String ssid = "";
String password = "";
String jiraHost = ""; 
String base64Credentials = ""; 

unsigned long lastActivityTime = 0;
const unsigned long displayTimeout = 30000; 
bool isDisplayOn = true;

// Keypad Definition
const byte ROWS = 4; 
const byte COLS = 5; 
char hexaKeys[ROWS][COLS] = {
  {'4','8','B','F','K'},
  {'3','7','A','E','I'},
  {'2','6','0','D','H'},
  {'1','5','9','C','G'}
};
byte rowPins[ROWS] = {41, 42, 2, 1}; 
byte colPins[COLS] = {40, 39, 45, 21, 14};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// Arrays für Keys und die Klarnamen (Summaries) im RAM
String taskMapping[20];
String taskNames[20];
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

void resetDisplayTimeout() {
  lastActivityTime = millis();
  if (!isDisplayOn) {
    digitalWrite(BACKLIGHT_PIN, HIGH); 
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
    // Zeige den echten Namen an (gekürzt auf 20 Zeichen fürs Display)
    String displayName = taskNames[activeTimerIndex];
    if (displayName.length() == 0 || displayName == "Unbekannter Task") {
      displayName = taskMapping[activeTimerIndex];
    }
    if (displayName.length() > 20) displayName = displayName.substring(0, 17) + "...";
    lcd.print(displayName);
  } else {
    lcd.print("Status: Bereit.");
  }
  
  lcd.setCursor(0, 3);
  lcd.print("Klick=Start Hlt=Zeit");
}

// Liest einen String über die serielle Schnittstelle ein (blockierend fürs Setup)
String readSerialLine() {
  while (!Serial.available()) {
    delay(50);
  }
  String input = Serial.readStringUntil('\n');
  input.trim(); // Zeilenumbrüche und Leerzeichen am Ende entfernen
  return input;
}

// Führt den Benutzer durch die Ersteinrichtung im Seriellen Monitor
void runSerialSetup() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("!!! SETUP MODUS !!!");
  lcd.setCursor(0, 1); lcd.print("Bitte den Seriellen");
  lcd.setCursor(0, 2); lcd.print("Monitor am PC oeff-");
  lcd.setCursor(0, 3); lcd.print("nen (115200 Baud).");

  Serial.println("\n==================================================");
  Serial.println("         ERSTEINRICHTUNG JIRA TRACKER             ");
  Serial.println("==================================================");
  
  Serial.print("1. Bitte WLAN Name (SSID) eingeben: ");
  ssid = readSerialLine();
  Serial.println(ssid);

  Serial.print("2. Bitte WLAN Passwort eingeben: ");
  password = readSerialLine();
  Serial.println("********");

  Serial.print("3. Bitte Jira-Host eingeben (z.B. deine-firma.atlassian.net): ");
  jiraHost = readSerialLine();
  Serial.println(jiraHost);

  Serial.print("4. Bitte Base64 Credentials eingeben: ");
  base64Credentials = readSerialLine();
  Serial.println("[GESPEICHERT]");

  // In Preferences permanent sichern
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.putString("jira_host", jiraHost);
  preferences.putString("jira_cred", base64Credentials);

  Serial.println("\n--> Alle Zugangsdaten erfolgreich gespeichert! Starte System neu...");
  lcd.clear();
  lcd.setCursor(0, 1); lcd.print("Data saved!");
  lcd.setCursor(0, 2); lcd.print("Restarting system...");
  delay(2000);
  ESP.restart();
}

String fetchTaskSummary(String issueKey) {
  if (WiFi.status() != WL_CONNECTED) return "Kein WLAN";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = "https://" + jiraHost + "/rest/api/3/issue/" + issueKey + "?fields=summary";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + base64Credentials);
  http.addHeader("Accept", "application/json");
  
  int httpCode = http.GET();
  String summary = "Unbekannter Task";
  
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error && doc["fields"]["summary"].is<const char*>()) {
      summary = doc["fields"]["summary"].as<String>();
    }
  }
  http.end();
  return summary;
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  lcd.begin(20, 4);
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Booting System...");

  // Preferences öffnen (Namespace "jira_tasks")
  preferences.begin("jira_tasks", false);

  // --- HARDWARE CONFIG RESET PRÜFUNG ---
  // Wir lesen die Tastmatrix direkt aus. Wenn beim Starten 'K' und 'D' gehalten werden -> Reset!
  customKeypad.getKeys(); // Tastenstatus aktualisieren
  bool resetPressed = false;
  
  // Prüfen ob K und D aktiv sind
  bool hasK = false, hasD = false;
  for (int i=0; i<LIST_MAX; i++) {
    if (customKeypad.key[i].kchar == 'K' && (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) hasK = true;
    if (customKeypad.key[i].kchar == 'D' && (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) hasD = true;
  }
  
  if (hasK && hasD) {
    Serial.println("\n[RESET] K und D beim Start erkannt. Loesche Zugangsdaten...");
    preferences.remove("wifi_ssid");
    preferences.remove("wifi_pass");
    preferences.remove("jira_host");
    preferences.remove("jira_cred");
  }

  // Zugangsdaten aus dem Speicher laden
  ssid = preferences.getString("wifi_ssid", "");
  password = preferences.getString("wifi_pass", "");
  jiraHost = preferences.getString("jira_host", "");
  base64Credentials = preferences.getString("jira_cred", "");

  // Falls unvollständig, erzwinge das serielle Setup
  if (ssid == "" || password == "" || jiraHost == "" || base64Credentials == "") {
    runSerialSetup();
  }

  // Task-Mappings (Tasten 1-20) laden
  for (int i = 0; i < 20; i++) {
    String keyName = "task_" + String(i);
    String titleName = "name_" + String(i);
    taskMapping[i] = preferences.getString(keyName.c_str(), "PROJ-" + String(i + 1));
    taskNames[i] = preferences.getString(titleName.c_str(), "Task " + String(i + 1));
  }

  customKeypad.setHoldTime(2000); 

  lcd.setCursor(0, 1); lcd.print("Connecting to WLAN..");
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // Timeout für WLAN einbauen (falls falsche Daten eingegeben wurden)
  int wifiTimeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
    wifiTimeoutCounter++;
    if (wifiTimeoutCounter > 30) { // Nach 15 Sekunden Fehlermeldung und Setup erzwingen
      Serial.println("\nWLAN Verbindung fehlgeschlagen! Starte Setup...");
      preferences.remove("wifi_ssid"); // SSID löschen damit er beim nächsten Boot ins Setup springt
      lcd.clear(); lcd.setCursor(0, 1); lcd.print("WLAN Fehler!");
      delay(2000);
      ESP.restart();
    }
  }
  
  resetDisplayTimeout();
  updateDefaultDisplay();
}

void loop() {
  char key = customKeypad.getKey();
  KeyState kpadState = customKeypad.getState();

  // Automatische Display-Abschaltung nach 30 Sekunden Inaktivität
  if (isDisplayOn && (millis() - lastActivityTime > displayTimeout)) {
    lcd.clear();
    digitalWrite(BACKLIGHT_PIN, LOW); 
    isDisplayOn = false;
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
      if (isShiftPressed && key != 'K' && key != 'D' && kpadState == PRESSED && idx != -1) {
        configIndex = idx;
        currentInput = "";
        lastT9Key = '\0';
        
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("!!! T9-SETUP !!!");
        lcd.setCursor(0, 1); lcd.print("Taste " + String(key) + " -> Jira Key");
        lcd.setCursor(0, 2); lcd.print("Eingabe: ");
        lcd.setCursor(0, 3); lcd.print("D=Del F=Save");
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
          lcd.setCursor(0, 0); lcd.print("MANUELLE ZEIT FUER:");
          lcd.setCursor(0, 1); lcd.print(taskMapping[manualTimeIndex]);
          lcd.setCursor(0, 2); lcd.print("Minuten: ");
          lcd.setCursor(0, 3); lcd.print("D=Del F=Buchen");
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
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Speichere Key...");
      lcd.setCursor(0, 1); lcd.print(currentInput);
      
      // 1. Key im RAM und NVS sichern
      taskMapping[configIndex] = currentInput;
      String keyName = "task_" + String(configIndex);
      preferences.putString(keyName.c_str(), currentInput);
      
      // 2. ERWEITERUNG: API-Abfrage für den echten Namen
      lcd.setCursor(0, 2); lcd.print("Lade Jira-Namen...");
      String fetchedName = fetchTaskSummary(currentInput);
      
      taskNames[configIndex] = fetchedName;
      String titleName = "name_" + String(configIndex);
      preferences.putString(titleName.c_str(), fetchedName);
      
      lcd.clear(); 
      lcd.setCursor(0, 1); lcd.print("Erfolgreich!");
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
    lcd.setCursor(0, 0); lcd.print("Buche Task:");
    lcd.setCursor(0, 1); lcd.print(taskMapping[index]);
    
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
  String url = "https://" + jiraHost + "/rest/api/3/issue/" + String(issueKey) + "/worklog";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + base64Credentials);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  String jsonPayload = "{\"timeSpent\": \"" + String(minutes) + "m\", \"comment\": {\"type\": \"doc\", \"version\": 1, \"content\": [{\"type\": \"paragraph\", \"content\": [{\"text\": \"Gebucht via ESP32 HD44780 Station.\", \"type\": \"text\"}]}]}}";
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