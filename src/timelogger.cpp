#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Keypad.h>
#include <Preferences.h>
#include <LiquidCrystal.h>
#include <ArduinoJson.h>

Preferences preferences;
String VERSION = "v1.03";

// HD44780 LCD Pins: LiquidCrystal lcd(rs, en, d4, d5, d6, d7)
LiquidCrystal lcd(9, 46, 8, 16, 15, 7);
const int BACKLIGHT_PIN = 4;

// WiFi and Jira configuration variables
String ssid = "";
String password = "";
String jiraHost = ""; 
String base64Credentials = ""; 
unsigned long maxTimerHours = 10; // Default maximum tracking duration in hours

unsigned long lastActivityTime = 0;
const unsigned long displayTimeout = 60000; 
bool isDisplayOn = true;
bool displayNameToggle = true;

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
int confirmTrackingIndex = -1;
unsigned long confirmDurationMinutes = 0;
int nextTimerIndexToStart = -1;

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
void handleConfirmationInput(char key);
void logTimeToJira(const char* issueKey, unsigned long minutes);
void enterLowPowerMode();

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

// Map keypad keys to T9 layout indices
int getT9LayoutIndex(char key)
{
  if (key >= '1' && key <= '9') return key - '1';
  if (key == '0') return 9;
  if (key == 'G') return 10;
  return -1;
}

// Reset display sleep timer
void resetDisplayTimeout()
{
  lastActivityTime = millis();
}

// Render the current UI state to the LCD
void updateDefaultDisplay()
{
  lcd.clear();
  
  // There is an active timer running
  if(activeTimerIndex != -1)
  {
    // Show real task name, if available
    lcd.setCursor(0, 0);
    String displayName = taskMapping[activeTimerIndex];
    lcd.print(displayName);
    lcd.setCursor(0, 1);
    displayName = taskNames[activeTimerIndex];
    if (displayName.length() > 20) displayName = displayName.substring(0, 17) + "...";
    lcd.print(displayName);
  }
  // No active timer running -> show Jira ID overview matrix
  else
  {
    for(int line=0; line < 4; line++) {
      for (int j=0; j < 4; j++) {
        lcd.setCursor(j*5, line);
        String displayName = taskMapping[line*4+j];
        int dashidx = displayName.indexOf('-');

        // Toggle between Project Key and Issue Number display
        if(displayNameToggle) {
          displayName = displayName.substring(0, dashidx);
          if (displayName.length() > 4) {
            displayName = displayName.substring(0, 4);
          }
        }
        else {
          displayName = displayName.substring(dashidx+1, 10);
          if (displayName.length() > 4) {
            displayName = displayName.substring(dashidx+1, dashidx+5);
          }
        }
        lcd.print(displayName);
      }
    }
  }
}

// Robustly read a line from serial interface filtering out both Carriage Return and Line Feed
String readSerialLine()
{
  String input = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        // If we hit a newline character and already have content, we finish reading
        if (input.length() > 0) {
          break;
        }
      } else {
        input += c;
      }
    }
    delay(5);
  }
  input.trim();
  return input;
}

// Interactive configuration wizard via hardware serial terminal
void runSerialSetup()
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("---- SETUP MODE ----");
  lcd.setCursor(0, 1); lcd.print("Please connect via  ");
  lcd.setCursor(0, 2); lcd.print("serial terminal and ");
  lcd.setCursor(0, 3); lcd.print("use 115200 baud @8N1");

  // Load existing values first so they are up to date in the menu
  ssid = preferences.getString("wifi_ssid", "");
  password = preferences.getString("wifi_pass", "");
  jiraHost = preferences.getString("jira_host", "");
  base64Credentials = preferences.getString("jira_cred", "");
  maxTimerHours = preferences.getUInt("max_hours", 10);

  while (true) {
    Serial.println("\n==================================================");
    Serial.println("         CONFIGURATION OF TIME TRACKER            ");
    Serial.println("==================================================");
    Serial.print("1. WiFi Name (SSID):         "); Serial.println(ssid == "" ? "[NOT SET]" : ssid);
    Serial.print("2. WiFi Password:            "); Serial.println(password == "" ? "[NOT SET]" : "********");
    Serial.print("3. Jira-Host:                "); Serial.println(jiraHost == "" ? "[NOT SET]" : jiraHost);
    Serial.print("4. Base64 Credentials:       "); Serial.println(base64Credentials == "" ? "[NOT SET]" : "[SAVED]");
    Serial.print("5. Max Timer Duration:       "); Serial.print(maxTimerHours); Serial.println(" Hours");
    Serial.println("--------------------------------------------------");
    Serial.println("6. Restart System");
    Serial.println("==================================================");
    Serial.print("Please select an option (1-6): ");

    String choice = readSerialLine();
    Serial.println(choice);

    if (choice == "1") {
      Serial.print("Enter new WiFi Name (SSID): ");
      ssid = readSerialLine();
      Serial.println(ssid);
      preferences.putString("wifi_ssid", ssid);
      Serial.println("--> WiFi Name updated in storage.");
    } 
    else if (choice == "2") {
      Serial.print("Enter new WiFi Password: ");
      password = readSerialLine();
      Serial.println("********");
      preferences.putString("wifi_pass", password);
      Serial.println("--> WiFi Password updated in storage.");
    } 
    else if (choice == "3") {
      Serial.print("Enter new Jira-Host (e.g. company.atlassian.net): ");
      jiraHost = readSerialLine();
      Serial.println(jiraHost);
      preferences.putString("jira_host", jiraHost);
      Serial.println("--> Jira-Host updated in storage.");
    } 
    else if (choice == "4") {
      Serial.print("Enter new Base64 Credentials: ");
      base64Credentials = readSerialLine();
      Serial.println("[SAVED]");
      preferences.putString("jira_cred", base64Credentials);
      Serial.println("--> Base64 Credentials updated in storage.");
    } 
    else if (choice == "5") {
      Serial.print("Enter max timer duration (in hours): ");
      String hrsInput = readSerialLine();
      long parsedHrs = hrsInput.toInt();
      if (parsedHrs > 0) {
        maxTimerHours = parsedHrs;
        Serial.print(maxTimerHours); Serial.println(" Hours");
        preferences.putUInt("max_hours", maxTimerHours);
        Serial.println("--> Max timer duration updated in storage.");
      } else {
        Serial.println("Invalid input! Must be a positive number.");
      }
    } 
    else if (choice == "6") {
      Serial.println("\n--> All settings successfully verified! Restarting system...");
      break;
    } 
    else {
      Serial.print("Invalid selection! You entered '"); Serial.print(choice); Serial.println("'. Please enter a number between 1 and 6.");
    }
    delay(500);
  }

  lcd.clear();
  lcd.setCursor(0, 1); lcd.print("Data saved!");
  lcd.setCursor(0, 2); lcd.print("Restarting system...");
  delay(2000);
  ESP.restart();
}

// Fetch the issue summary title string from Jira REST API
String fetchTaskSummary(String issueKey)
{
  if (WiFi.status() != WL_CONNECTED) return "No WiFi!";
  WiFiClientSecure client;
  client.setInsecure(); // Skip SSL certificate verification for simplicity
  HTTPClient http;
  
  String url = "https://" + jiraHost + "/rest/api/3/issue/" + issueKey + "?fields=summary";
  http.begin(client, url);
  http.addHeader("Authorization", "Basic " + base64Credentials);
  http.addHeader("Accept", "application/json");
  
  int httpCode = http.GET();
  String summary = "Unknown Task";
  
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error && doc["fields"]["summary"].is<const char*>()) {
      summary = doc["fields"]["summary"].as<String>();
    }
  }
  http.end();
  return summary;
}

// Process key entries during manual minute input mode
void handleManualTimeInput(char key)
{
  if (key == 'C') // Confirm and upload entry
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

  if (key == 'B') // Delete last character / Cancel mode if empty
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

  // Append digit to input buffer
  if ((key >= '1' && key <= '9') || key == '0') {
    char digit = (key == '0') ? '0' : key;
    currentInput += digit;
    lcd.setCursor(9, 2); lcd.print(currentInput);
  }
}

// Process character cycling for T9 Jira ID string editing
void handleT9Input(char key)
{
  if (key == 'C') // Confirm and fetch Jira details
  {
    if (currentInput.length() > 0) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Saving Jira ID...");
      lcd.setCursor(0, 1); lcd.print(currentInput);
      
      // 1. Save Key in RAM and NVS
      taskMapping[configIndex] = currentInput;
      String keyName = "task_" + String(configIndex);
      preferences.putString(keyName.c_str(), currentInput);
      
      // 2. Request real task name from Jira API
      lcd.setCursor(0, 2); lcd.print("Loading Jira-Name...");
      String fetchedName = fetchTaskSummary(currentInput);
      
      taskNames[configIndex] = fetchedName;
      String titleName = "name_" + String(configIndex);
      preferences.putString(titleName.c_str(), fetchedName);
      
      if (fetchedName.length() > 20) fetchedName = fetchedName.substring(0, 17) + "...";
      lcd.setCursor(0, 3); lcd.print(fetchedName);
      delay(1500);
    }
    configIndex = -1;
    updateDefaultDisplay();
    return;
  }

  if (key == 'B') // Backspace / Cancel mode if empty
  {
    if (currentInput.length() > 0) {
      currentInput.remove(currentInput.length() - 1);
      lastT9Key = '\0';
      lcd.setCursor(7, 2); lcd.print(currentInput + "    ");
    }
    else {
      configIndex = -1;
      updateDefaultDisplay();
    }
    return;
  }

  // Handle T9 character cycling timeout and tracking
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
    lcd.setCursor(7, 2); lcd.print(currentInput);
  }
}

// Logic hook for starting or prompting to log timers
void handleTracking(int index)
{
  // Case 1: A timer was running for the actual task -> open stop-confirmation prompt
  if(startTimes[index] != 0) {
    confirmDurationMinutes = (millis() - startTimes[index]) / 60000;
    if(confirmDurationMinutes == 0) {
      confirmDurationMinutes = 1;
    }
    confirmTrackingIndex = index;
    nextTimerIndexToStart = -1; // No context switch needed after dialog
    
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Book logged time?");
    lcd.setCursor(0, 1); lcd.print(taskMapping[index]);
    lcd.setCursor(0, 2); lcd.print("Time: " + String(confirmDurationMinutes) + " Min");
    lcd.setCursor(0, 3); lcd.print("Cancel=No Enter=Yes");
  } 
  // Case 2: A timer was running for a different task -> confirm booking old task and auto-schedule new task
  else
  {
    if(activeTimerIndex != -1) {
      confirmDurationMinutes = (millis() - startTimes[activeTimerIndex]) / 60000;
      if (confirmDurationMinutes == 0) confirmDurationMinutes = 1;
      
      confirmTrackingIndex = activeTimerIndex;
      nextTimerIndexToStart = index; // Queue up next task index to spin up after dialog finishes
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Book logged time?");
      lcd.setCursor(0, 1); lcd.print(taskMapping[activeTimerIndex]);
      lcd.setCursor(0, 2); lcd.print("Time: " + String(confirmDurationMinutes) + " Min");
      lcd.setCursor(0, 3); lcd.print("Cancel=No Enter=Yes");
    }
    // Case 3: No timer was running at all -> spin up new timer instantly
    else {
      startTimes[index] = millis();
      activeTimerIndex = index;
      updateDefaultDisplay();
    }
  }
}

// Handle the user response inside the confirmation dialog window
void handleConfirmationInput(char key)
{
  if (key == 'C') { // Log to Jira (Enter=Yes)
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Sending to Jira...");
    if (WiFi.status() == WL_CONNECTED) {
      logTimeToJira(taskMapping[confirmTrackingIndex].c_str(), confirmDurationMinutes);
    } else {
      lcd.setCursor(0, 2);
      lcd.print("WiFi Error!");
      delay(2000);
    }
    startTimes[confirmTrackingIndex] = 0;
    
    // Check if another task timer needs to spin up (Context switch scenario)
    if (nextTimerIndexToStart != -1) {
      startTimes[nextTimerIndexToStart] = millis();
      activeTimerIndex = nextTimerIndexToStart;
      nextTimerIndexToStart = -1;
    } else {
      activeTimerIndex = -1;
    }
    confirmTrackingIndex = -1;
    updateDefaultDisplay();
  } 
  else if (key == 'B') { // Discard tracked time (Cancel=No)
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(" Time log discarded ");
    delay(1500);
    startTimes[confirmTrackingIndex] = 0;
    
    // Start queued timer even if previous log was discarded
    if (nextTimerIndexToStart != -1) {
      startTimes[nextTimerIndexToStart] = millis();
      activeTimerIndex = nextTimerIndexToStart;
      nextTimerIndexToStart = -1;
    } else {
      activeTimerIndex = -1;
    }
    confirmTrackingIndex = -1;
    updateDefaultDisplay();
  }
}

// Assemble JSON payload and execute POST request to Jira REST API endpoint
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

// Enter light sleep mode
void enterLowPowerMode() 
{
  // Turn off LCD completely to stop power drain
  lcd.clear();
  digitalWrite(BACKLIGHT_PIN, LOW);
  isDisplayOn = false;

  // Reconfigure pins so that every key press leads to a wake-up
  for (int i = 0; i < COLS; i++) {
    pinMode(colPins[i], OUTPUT);
    digitalWrite(colPins[i], LOW);
  }
  for (int i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], INPUT_PULLUP);
    gpio_num_t rowPin = (gpio_num_t)rowPins[i];
    gpio_wakeup_enable(rowPin, GPIO_INTR_LOW_LEVEL);
  }
  delay(1); 

  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();

  // --- WOKEN UP FROM LIGHT SLEEP HERE ---
  for (int i = 0; i < ROWS; i++) {
    gpio_num_t rowPin = (gpio_num_t)rowPins[i];
    gpio_wakeup_disable(rowPin);
  }
  
  for (int i = 0; i < COLS; i++) {
    pinMode(colPins[i], INPUT); 
  }
  for (int i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], INPUT); 
  }
  
  digitalWrite(BACKLIGHT_PIN, HIGH);
  resetDisplayTimeout();
  updateDefaultDisplay();
}

void setup()
{
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  lcd.begin(20, 4);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Datapad " + VERSION + " boot..");

  Serial.begin(115200);
  delay(2000); // Wait for USB-Serial controller to initialize after a power-cycle

  preferences.begin("jira_tasks", false);

  // Check for simultaneous press of Shift ('S') and Enter ('C') to force setup mode bypass
  customKeypad.getKeys();
  bool hasS = false, hasC = false;
  for (int i=0; i<LIST_MAX; i++) {
    if (customKeypad.key[i].kchar == 'S' && (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) hasS = true;
    if (customKeypad.key[i].kchar == 'C' && (customKeypad.key[i].kstate == PRESSED || customKeypad.key[i].kstate == HOLD)) hasC = true;
  }
  if (hasS && hasC) {
    runSerialSetup();
  }

  // Load WiFi and Jira access data from non-volatile preferences
  ssid = preferences.getString("wifi_ssid", "");
  password = preferences.getString("wifi_pass", "");
  jiraHost = preferences.getString("jira_host", "");
  base64Credentials = preferences.getString("jira_cred", "");
  maxTimerHours = preferences.getUInt("max_hours", 10); // Load max hours from NVS (defaults to 10 if not set)

  // Fall back to configuration mode if credentials are empty
  if (ssid == "" || password == "" || jiraHost == "" || base64Credentials == "") {
    runSerialSetup();
  }

  // Populate task runtime parameters from storage slots
  for (int i = 0; i < 20; i++) {
    String keyName = "task_" + String(i);
    String titleName = "name_" + String(i);
    taskMapping[i] = preferences.getString(keyName.c_str(), "*****-*****");
    taskNames[i] = preferences.getString(titleName.c_str(), "Task " + String(i + 1));
  }

  customKeypad.setHoldTime(500);
  customKeypad.setDebounceTime(50);

  // Connect to WiFi
  lcd.setCursor(0, 1); lcd.print("Connecting to WiFi..");
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // WiFi Timeout
  int wifiTimeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
    wifiTimeoutCounter++;
    if (wifiTimeoutCounter > 30) { // Timeout reached after roughly 15s -> abort and restart node
      Serial.println("\nError establishing WiFi connection! Restarting...");
      lcd.setCursor(0, 1); lcd.print("WiFi Error!         ");
      lcd.setCursor(0, 2); lcd.print("Hold Shift + Enter  ");
      lcd.setCursor(0, 3); lcd.print("to enter Setup Mode ");
      delay(3000);
      ESP.restart();
    }
  }
  
  resetDisplayTimeout();
  updateDefaultDisplay();
}

void loop()
{
  bool isShiftPressed = false;
  bool isAPressed = false;
  static unsigned long lastUpdateTime = 0;

  // Automatic display timeout (except in T9-entry mode, manual-entry mode and confirmation dialog)
  if(configIndex == -1 && manualTimeIndex == -1 && confirmTrackingIndex == -1 &&
    (millis() - lastActivityTime > displayTimeout))
  {
    enterLowPowerMode();
  }

  // Handle character selection timeout inside T9 layout engine
  if(configIndex != -1 && currentInput.length() > 0 && (millis() - lastT9Time > t9Timeout) && lastT9Key != '\0') {
    lastT9Key = '\0'; 
    lcd.setCursor(7, 2);
    lcd.print(currentInput + " ");
  }

  // Fills customKeypad.key[] array with up-to 10 active keys. Returns true if there are ANY active keys.
  if(customKeypad.getKeys())
  {
    // Catch the first keystroke event after the display was off / sleep
    if(!isDisplayOn) {
      isDisplayOn = true;
      return;
    }
    resetDisplayTimeout(); 
    
    // EVALUATE FIRST KEY
    char firstKey = customKeypad.key[0].kchar;
    KeyState firstState = customKeypad.key[0].kstate;

    if (confirmTrackingIndex != -1) {
      if (firstState == PRESSED) {
        handleConfirmationInput(firstKey);
      }
      return;
    }
    
    if (configIndex != -1) {
      if (firstState == PRESSED) {
        handleT9Input(firstKey);
      }
      return;
    }

    if (manualTimeIndex != -1) {
      if (firstState == PRESSED) {
        handleManualTimeInput(firstKey);
      }
      return;
    }

    // Check if 'S' (Shift) or 'A' is active on the first key position
    if(firstKey == 'S' && (firstState == PRESSED || firstState == HOLD)) {
      isShiftPressed = true;
    }
    else if(firstKey == 'A' && (firstState == PRESSED || firstState == HOLD)) {
      isAPressed = true;
    }
    else {
      // Single Key Action (No Shift, No 'A')
      int taskidx = getKeyIndex(firstKey);
      if(firstState == PRESSED && taskidx != -1) {
        handleTracking(taskidx);
      }
    }
    
    // Evaluate multi-key state (Shift + Task-Key or A + Task-Key)
    if(customKeypad.key[1].kchar != NO_KEY && 
      (customKeypad.key[1].kstate == PRESSED || customKeypad.key[1].kstate == HOLD))
    {
      int taskidx = getKeyIndex(customKeypad.key[1].kchar);

      // Shift + Task Key combination -> Switch into T9 Setup Editor Mode
      if(isShiftPressed && taskidx != -1) {
        configIndex = taskidx;
        currentInput = "";
        lastT9Key = '\0';
        
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Enter Jira Task ID!");
        lcd.setCursor(0, 1); lcd.print("Key " + String(customKeypad.key[1].kchar) + " -> Jira ID");
        lcd.setCursor(0, 2); lcd.print("Entry: ");
        lcd.setCursor(0, 3); lcd.print("Cancel=Del Enter=OK");
      }

      // Action 'A' + Task Key combination -> Switch into Direct Manual Minute Input Mode
      if(isAPressed && taskidx != -1) {
        manualTimeIndex = taskidx;
        currentInput = "";
        
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Manual entry for:");
        lcd.setCursor(0, 1); lcd.print(taskMapping[manualTimeIndex]);
        lcd.setCursor(0, 2); lcd.print("Minutes: ");
        lcd.setCursor(0, 3); lcd.print("Cancel=Del Enter=Log");
      }
    }
  }

  // Refresh interval tick loop for Idle Jira Matrix Grid views (4-second cycle)
  if(activeTimerIndex == -1 && configIndex == -1 && manualTimeIndex == -1 &&
    confirmTrackingIndex == -1 && ((millis() - lastUpdateTime) > 4000))
  {
    lastUpdateTime = millis();
    displayNameToggle = !displayNameToggle;
    updateDefaultDisplay();
  }

  // Update live counter seconds for running timer
  if(activeTimerIndex != -1 && configIndex == -1 && manualTimeIndex == -1 &&
    confirmTrackingIndex == -1)
  {
    unsigned long elapsedMillis = millis() - startTimes[activeTimerIndex];
    unsigned long maxAllowedMillis = maxTimerHours * 3600000;

    // Force automatic timeout submission if max configuration threshold is hit
    if (elapsedMillis >= maxAllowedMillis) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Limit reached! (" + String(maxTimerHours) + "h)");
      lcd.setCursor(0, 1); lcd.print("Auto-logging task...");
      lcd.setCursor(0, 2); lcd.print(taskMapping[activeTimerIndex]);
      
      if (WiFi.status() == WL_CONNECTED) {
        logTimeToJira(taskMapping[activeTimerIndex].c_str(), maxTimerHours * 60);
      } else {
        lcd.setCursor(0, 3); lcd.print("WiFi Err, Log lost!");
        delay(3000);
      }
      
      startTimes[activeTimerIndex] = 0;
      activeTimerIndex = -1;
      updateDefaultDisplay();
    } 
    else {
      static unsigned long lastSecUpdate = 0;
      if (millis() - lastSecUpdate > 1000) {
        lastSecUpdate = millis();
        unsigned long totalSecs = elapsedMillis / 1000;
        unsigned long mins = totalSecs / 60;
        unsigned long secs = totalSecs % 60;
        
        lcd.setCursor(0, 2);
        char timeBuf[21];
        sprintf(timeBuf, "Running: %02lu:%02lu      ", mins, secs);
        lcd.print(timeBuf);
      }
    }
  }
  delay(10);
}