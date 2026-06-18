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

// WiFi and Jira configuration variables
String ssid = "";
String password = "";
String jiraHost = ""; 
String base64Credentials = ""; 

unsigned long lastActivityTime = 0;
const unsigned long displayTimeout = 60000; 
bool isDisplayOn = true;

// Keypad Definition (rows and cols are inverted here to match anti-ghosting diode polarity with library code)
const byte ROWS = 4; 
const byte COLS = 5; 
char hexaKeys[ROWS][COLS] = {
  {'S','7','4','1','0'},
  {'A','8','5','2','G'},
  {'B','9','6','3','H'},
  {'C','D','E','F','I'}
};
byte rowPins[ROWS] = {41, 42, 2, 1}; 
byte colPins[COLS] = {40, 39, 45, 21, 14};
int keyTaskMap[ROWS][COLS] = {
  {-1,0,4,8,12},
  {-1,1,5,9,13},
  {-1,2,6,10,14},
  {-1,3,7,11,15}
};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

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

void resetDisplayTimeout();
void updateDefaultDisplay();
String readSerialLine();
void runSerialSetup();
String fetchTaskSummary(String issueKey);
void handleManualTimeInput(char key);
void handleT9Input(char key);
void handleTracking(int index);
void logTimeToJira(const char* issueKey, unsigned long minutes);

// Return the index of the keypad's key-to-task mapping
int getKeyIndex(char key)
{
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      if (hexaKeys[i][j] == key) {
        return keyTaskMap[i][j];
      }
    }
  }
  return -1;
}

int getT9LayoutIndex(char key)
{
  if (key >= '1' && key <= '9') return key - '1';
  if (key == 'A') return 9;
  if (key == 'B') return 10;
  return -1;
}

void resetDisplayTimeout()
{
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

void runSerialSetup()
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("---- SETUP MODE ----");
  lcd.setCursor(0, 1); lcd.print("Please connect via  ");
  lcd.setCursor(0, 2); lcd.print("serial terminal and ");
  lcd.setCursor(0, 3); lcd.print("use 115200 baud 8N1 ");

  Serial.println("\n==================================================");
  Serial.println("        CONFIGURATION OF TIME TRACKER             ");
  Serial.println("==================================================");
  
  Serial.print("1. Please enter WiFi Name (SSID): ");
  ssid = readSerialLine();
  Serial.println(ssid);

  Serial.print("2. Please enter WiFi Password: ");
  password = readSerialLine();
  Serial.println("********");

  Serial.print("3. Please enter Jira-Host (e.g. company.atlassian.net): ");
  jiraHost = readSerialLine();
  Serial.println(jiraHost);

  Serial.print("4. Please enter Base64 Credentials: ");
  base64Credentials = readSerialLine();
  Serial.println("[SAVED]");

  // Save to Preferences
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.putString("jira_host", jiraHost);
  preferences.putString("jira_cred", base64Credentials);

  Serial.println("\n--> All settings successfully saved! Restarting system...");
  lcd.clear();
  lcd.setCursor(0, 1); lcd.print("Data saved!");
  lcd.setCursor(0, 2); lcd.print("Restarting system...");
  delay(2000);
  ESP.restart();
}

String fetchTaskSummary(String issueKey)
{
  if (WiFi.status() != WL_CONNECTED) return "No WiFi!";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = "https://" + jiraHost + "/rest/api/3/issue/" + issueKey + "?fields=summary";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + base64Credentials);
  http.addHeader("Accept", "application/json");
  
  int httpCode = http.GET();
  String summary = "Unknown Task";
  
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

void handleManualTimeInput(char key)
{
  if (key == 'C') // Confirm
  {
    lcd.clear();
    if (currentInput.length() > 0) {
      long minutes = currentInput.toInt();
      if (minutes > 0) {
        lcd.setCursor(0, 0); lcd.print("Sending to Jira...");
        lcd.setCursor(0, 1); lcd.print(taskMapping[manualTimeIndex] + ": " + String(minutes) + "m");
        if (WiFi.status() == WL_CONNECTED) {
          logTimeToJira(taskMapping[manualTimeIndex].c_str(), minutes);
        } else {
          lcd.setCursor(0, 2); lcd.print("WiFi Error!");
          delay(2000);
        }
      }
    }
    manualTimeIndex = -1;
    updateDefaultDisplay();
    return;
  }

  if (key == 'B') // Delete last character / Cancel
  {
    if (currentInput.length() > 0) {
      currentInput.remove(currentInput.length() - 1);
      lcd.setCursor(9, 2); lcd.print(currentInput + "    ");
    }
    else {
      manualTimeIndex = -1;
      updateDefaultDisplay();
    }
    return;
  }

  if ((key >= '1' && key <= '9') || key == '0') {
    char digit = (key == '0') ? '0' : key;
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
      lcd.setCursor(0, 2); lcd.print("WiFi Error!");
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

void logTimeToJira(const char* issueKey, unsigned long minutes)
{
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  String url = "https://" + jiraHost + "/rest/api/3/issue/" + String(issueKey) + "/worklog";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + base64Credentials);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  String jsonPayload = "{\"timeSpent\": \"" + String(minutes) + "m\", \"comment\": {\"type\": \"doc\", \"version\": 1, \"content\": [{\"type\": \"paragraph\", \"content\": [{\"text\": \"Logged via ESP32 Datapad.\", \"type\": \"text\"}]}]}}";
  int httpResponseCode = http.POST(jsonPayload);
  
  lcd.setCursor(0, 2);
  if (httpResponseCode == 201) {
    lcd.print("Time logged!        ");
  } else {
    lcd.print("Err HTTP:" + String(httpResponseCode));
  }
  delay(1500); 
  http.end();
}

void setup()
{
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  lcd.begin(20, 4);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Datapad booting...");

  Serial.begin(115200);
  delay(2000); // Wait for USB-Serial controller to initialize after a power-cycle

  preferences.begin("jira_tasks", false);

  // Check for simultaneous press of S and A to reset configuration
  customKeypad.getKeys();
  bool hasS = false, hasA = false;
  for (int i=0; i<LIST_MAX; i++) {
    if (customKeypad.key[i].kchar == 'S' && (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) hasS = true;
    if (customKeypad.key[i].kchar == 'A' && (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) hasA = true;
  }
  if (hasS && hasA) {
    runSerialSetup();
  }

  // Load WiFi and Jira access data from EEPROM
  ssid = preferences.getString("wifi_ssid", "");
  password = preferences.getString("wifi_pass", "");
  jiraHost = preferences.getString("jira_host", "");
  base64Credentials = preferences.getString("jira_cred", "");

  // Enter serial Setup Mode if any of the preferences could not be loaded
  if (ssid == "" || password == "" || jiraHost == "" || base64Credentials == "") {
    runSerialSetup();
  }

  // Load Task-Mappings
  for (int i = 0; i < 20; i++) {
    String keyName = "task_" + String(i);
    String titleName = "name_" + String(i);
    taskMapping[i] = preferences.getString(keyName.c_str(), "PROJ-" + String(i + 1));
    taskNames[i] = preferences.getString(titleName.c_str(), "Task " + String(i + 1));
  }

  customKeypad.setHoldTime(2000);
  customKeypad.setDebounceTime(10);

  // Connect to WiFi
  lcd.setCursor(0, 1); lcd.print("Connecting to WiFi..");
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // WiFi Timeout
  int wifiTimeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
    wifiTimeoutCounter++;
    if (wifiTimeoutCounter > 30) { // Print error message after 15s and restart
      Serial.println("\nError establishing WiFi connection! Restarting...");
      lcd.clear(); lcd.setCursor(0, 1); lcd.print("WiFi Error!");
      delay(3000);
      ESP.restart();
    }
  }
  
  resetDisplayTimeout();
  updateDefaultDisplay();
}

void loop()
{
  char key = customKeypad.getKey();
  KeyState kpadState = customKeypad.getState();

  // Automatic display timeout
  if (isDisplayOn && (millis() - lastActivityTime > displayTimeout)) {
    lcd.clear();
    digitalWrite(BACKLIGHT_PIN, LOW); 
    isDisplayOn = false;
  }
  // Turn on display on keypress
  if (!isDisplayOn && key) {
    resetDisplayTimeout();
    return;
  }

  // T9 Timeout für Buchstaben-Fixierung
  if (configIndex != -1 && currentInput.length() > 0 && (millis() - lastT9Time > t9Timeout) && lastT9Key != '\0') {
    lastT9Key = '\0'; 
    lcd.setCursor(9, 2);
    lcd.print(currentInput + " ");
  }

  // Check for 'S' (Shift) key being pressed
  bool isShiftPressed = false;
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      if (customKeypad.key[i].kchar == 'S' && 
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

  // Long press: direct time entry
  if (kpadState == HOLD && configIndex == -1 && manualTimeIndex == -1) {
    for (int i = 0; i < ROWS; i++) {
      if (customKeypad.key[i].kstate == HOLD) {
        char heldKey = customKeypad.key[i].kchar;
        int idx = getKeyIndex(heldKey);
        if (idx != -1 && heldKey != 'S' && heldKey != 'A' && heldKey != 'B' && heldKey != 'C') {
          resetDisplayTimeout();
          manualTimeIndex = idx;
          currentInput = "";
          
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Manual entry for:");
          lcd.setCursor(0, 1); lcd.print(taskMapping[manualTimeIndex]);
          lcd.setCursor(0, 2); lcd.print("Minutes: ");
          lcd.setCursor(0, 3); lcd.print("Enter=Book");
        }
      }
    }
  }

  // Update seconds in case of running timer
  if (isDisplayOn && activeTimerIndex != -1 && configIndex == -1 && manualTimeIndex == -1) {
    static unsigned long lastSecUpdate = 0;
    if (millis() - lastSecUpdate > 1000) {
      lastSecUpdate = millis();
      unsigned long totalSecs = (millis() - startTimes[activeTimerIndex]) / 1000;
      unsigned long mins = totalSecs / 60;
      unsigned long secs = totalSecs % 60;
      
      lcd.setCursor(0, 3);
      char timeBuf[21];
      sprintf(timeBuf, "Running: %02lu:%02lu      ", mins, secs);
      lcd.print(timeBuf);
    }
  }
  delay(10);
}
