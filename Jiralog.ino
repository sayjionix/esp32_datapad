#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Keypad.h>
#include <Preferences.h>

// WLAN Zugangsdaten
const char* ssid = "FRITZ!Box 7490";
const char* password = "98865512128174437592";

// Jira Konfiguration
const char* jiraHost = "txteaviation.atlassian.net"; 
const char* base64Credentials = "cC5sYXVmZnNAYW1hemlsaWEuYWVybzpBVEFUVDN4RmZHRjAxWVYzelhmSXJQcXRRQmVzWXFJOGRyeXNnTlo3X2JIVEthNGZhZnBPRUJwcXRMMmxXMm1MWHdFYkZObUpsaWZGWGpkRVJVbGNkV055TzZvLVk4QlhHMFJOcVk3T3A5a0JRVzc3RjBMeExGSjJZTzd4cHV3UHZ5ZlpYMzVhczVQVFJaN2xNNXhiMlFtZ0lvV0pSUkY5Y0UxY3I0cVZ4NmtxbzhMMTZJMzk3SDg9OUJEOTVFQjY"; 

Preferences preferences;

const byte ROWS = 5; 
const byte COLS = 4; 
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','4'},
  {'5','6','7','8'},
  {'9','A','B','C'},
  {'D','E','F','G'},
  {'H','I','J','K'} // 'K' ist die Setup/Shift-Taste
};
byte rowPins[ROWS] = {13, 12, 14, 27, 26}; 
byte colPins[COLS] = {25, 33, 32, 22}; 

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

String taskMapping[20];
unsigned long startTimes[20] = {0};

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

void setup() {
  Serial.begin(115200);
  delay(1000);

  preferences.begin("jira_tasks", false);
  for (int i = 0; i < 20; i++) {
    String keyName = "task_" + String(i);
    taskMapping[i] = preferences.getString(keyName.c_str(), "PROJ-" + String(i + 1));
  }

  customKeypad.setHoldTime(2000); 

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWLAN bereit!");
  Serial.println("[Klick = Start/Wechsel] [K halten + Klick = T9 Setup] [Lange halten = Zeit direkt tippen]");
}

void loop() {
  char key = customKeypad.getKey();
  KeyState kpadState = customKeypad.getState();

  if (configIndex != -1 && currentInput.length() > 0 && (millis() - lastT9Time > t9Timeout) && lastT9Key != '\0') {
    lastT9Key = '\0'; 
    Serial.print("\nBuchstabe fixiert. Aktueller Text: "); Serial.println(currentInput);
  }

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
    int idx = getKeyIndex(key);

    if (configIndex != -1) {
      handleT9Input(key);
    } 
    else if (manualTimeIndex != -1) {
      handleManualTimeInput(key);
    } 
    else {
      if (isShiftPressed && key != 'K' && kpadState == PRESSED && idx != -1) {
        configIndex = idx;
        currentInput = "";
        lastT9Key = '\0';
        Serial.println("\n====================================");
        Serial.printf("!!! T9-SETUP FÜR TASTE %c !!!\n", key);
        Serial.print("Aktueller Key: "); Serial.println(taskMapping[configIndex]);
        Serial.println("====================================");
      } 
      else if (!isShiftPressed && kpadState == PRESSED && idx != -1 && key != 'K') {
        handleTracking(idx);
      }
    }
  }

  if (kpadState == HOLD && configIndex == -1 && manualTimeIndex == -1) {
    for (int i = 0; i < ROWS; i++) {
      if (customKeypad.key[i].kstate == HOLD) {
        char heldKey = customKeypad.key[i].kchar;
        int idx = getKeyIndex(heldKey);
        if (idx != -1 && heldKey != 'K' && heldKey != 'D' && heldKey != 'F') {
          manualTimeIndex = idx;
          currentInput = "";
          Serial.println("\n====================================");
          Serial.printf("!!! DIREKTE ZEITEINGABE FÜR %s !!!\n", taskMapping[manualTimeIndex].c_str());
          Serial.println("====================================");
        }
      }
    }
  }

  delay(10);
}

void handleManualTimeInput(char key) {
  if (key == 'F') {
    if (currentInput.length() > 0) {
      long minutes = currentInput.toInt();
      if (minutes > 0) {
        Serial.printf("\nBuche direkt %ld Minuten auf Task %s...\n", minutes, taskMapping[manualTimeIndex].c_str());
        if (WiFi.status() == WL_CONNECTED) {
          logTimeToJira(taskMapping[manualTimeIndex].c_str(), minutes);
        } else {
          Serial.println("Fehler: Kein WLAN!");
        }
      }
    }
    manualTimeIndex = -1;
    return;
  }

  if (key == 'D') {
    if (currentInput.length() > 0) {
      currentInput.remove(currentInput.length() - 1);
      Serial.print("\rEingabe: "); Serial.print(currentInput); Serial.print("   ");
    }
    return;
  }

  if ((key >= '1' && key <= '9') || key == 'A') {
    char digit = (key == 'A') ? '0' : key;
    currentInput += digit;
    Serial.print("\rEingabe (Minuten): "); Serial.print(currentInput);
  }
}

void handleT9Input(char key) {
  if (key == 'F') {
    if (currentInput.length() > 0) {
      taskMapping[configIndex] = currentInput;
      String keyName = "task_" + String(configIndex);
      preferences.putString(keyName.c_str(), currentInput);
      Serial.printf("\n>>> Gespeichert: %s <<<\n", currentInput.c_str());
    }
    configIndex = -1;
    return;
  }

  if (key == 'D') {
    if (currentInput.length() > 0) {
      currentInput.remove(currentInput.length() - 1);
      lastT9Key = '\0';
      Serial.print("\nAktueller Text: "); Serial.println(currentInput);
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
    Serial.print("\rEingabe: "); Serial.print(currentInput);
  }
}

// HIER IST DIE NEUE LOGIK FÜR DAS AUTOMATISCHE BEENDEN
void handleTracking(int index) {
  String currentTask = taskMapping[index];
  
  // FALL 1: Der gedrückte Task läuft bereits -> Stoppen und buchen
  if (startTimes[index] != 0) {
    unsigned long durationMinutes = (millis() - startTimes[index]) / 60000;
    if (durationMinutes == 0) durationMinutes = 1;
    
    Serial.printf("[STOP] %s beendet | Dauer: %lu Min.\n", currentTask.c_str(), durationMinutes);
    if (WiFi.status() == WL_CONNECTED) {
      logTimeToJira(currentTask.c_str(), durationMinutes);
    } else {
      Serial.println("Fehler: Kein WLAN!");
    }
    startTimes[index] = 0; // Timer zurücksetzen
  } 
  
  // FALL 2: Der gedrückte Task läuft noch nicht
  else {
    // Vor dem Starten prüfen, ob IRGENDEIN ANDERER Task aktuell läuft
    for (int i = 0; i < 20; i++) {
      if (startTimes[i] != 0) {
        // Gefunden! Den alten Task erst stoppen und buchen
        unsigned long oldDurationMinutes = (millis() - startTimes[i]) / 60000;
        if (oldDurationMinutes == 0) oldDurationMinutes = 1;
        
        Serial.printf("\n[AUTO-STOP] Wechsel erkannt! Stoppe laufenden Task: %s\n", taskMapping[i].c_str());
        Serial.printf("[AUTO-STOP] Dauer: %lu Min. Sende an Jira...\n", oldDurationMinutes);
        
        if (WiFi.status() == WL_CONNECTED) {
          logTimeToJira(taskMapping[i].c_str(), oldDurationMinutes);
        } else {
          Serial.println("Fehler: Kein WLAN beim Auto-Stop!");
        }
        startTimes[i] = 0; // Alten Timer nullen
      }
    }
    
    // Jetzt den neuen Task wie gewohnt starten
    startTimes[index] = millis();
    Serial.printf("[START] Stoppuhr läuft ab jetzt für: %s\n", currentTask.c_str());
  }
}

void logTimeToJira(const char* issueKey, unsigned long minutes) {
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  String url = "https://" + String(jiraHost) + "/rest/api/3/issue/" + String(issueKey) + "/worklog";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + String(base64Credentials));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  String jsonPayload = "{\"timeSpent\": \"" + String(minutes) + "m\", \"comment\": {\"type\": \"doc\", \"version\": 1, \"content\": [{\"type\": \"paragraph\", \"content\": [{\"text\": \"Gebucht via ESP32 Radio-Matrix.\", \"type\": \"text\"}]}]}}";
  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode == 201) Serial.println(">>> Jira: Erfolgreich gebucht! <<<");
  else Serial.printf("Jira Fehler (HTTP %d)\n", httpResponseCode);
  http.end();
}