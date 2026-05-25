#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// WLAN Zugangsdaten
const char* ssid = "FRITZ!Box 7490";
const char* password = "98865512128174437592";

// Jira Konfiguration
const char* jiraHost = "txteaviation.atlassian.net"; 
const char* issueKey = "NBT-15"; 
const char* base64Credentials = "cC5sYXVmZnNAYW1hemlsaWEuYWVybzpBVEFUVDN4RmZHRjAxWVYzelhmSXJQcXRRQmVzWXFJOGRyeXNnTlo3X2JIVEthNGZhZnBPRUJwcXRMMmxXMm1MWHdFYkZObUpsaWZGWGpkRVJVbGNkV055TzZvLVk4QlhHMFJOcVk3T3A5a0JRVzc3RjBMeExGSjJZTzd4cHV3UHZ5ZlpYMzVhczVQVFJaN2xNNXhiMlFtZ0lvV0pSUkY5Y0UxY3I0cVZ4NmtxbzhMMTZJMzk3SDg9OUJEOTVFQjY"; 

// Hardware-Pins
const int BUTTON_PIN = 0; // GPIO 0 für den Taster

// Variablen für den Taster-Status (Entprellen)
volatile bool buttonPressed = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250; // 250ms Sperre nach Tastendruck

// Diese Funktion wird sofort aufgerufen, wenn der Knopf gedrückt wird
void IRAM_ATTR handleButtonPress() {
  unsigned long currentTime = millis();
  // Prüfen, ob die Entprell-Zeit abgelaufen ist
  if ((currentTime - lastDebounceTime) > debounceDelay) {
    buttonPressed = true;
    lastDebounceTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Button-Pin mit internem Pull-Up-Widerstand aktivieren
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Interrupt aktivieren: Reagiert, wenn der Pin von HIGH auf LOW fällt (FALLING)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);

  // WLAN direkt beim Start verbinden, damit wir sofort bereit sind
  Serial.print("Verbinde mit ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWLAN bereit! Drücke den Knopf, um 1 Stunde zu loggen.");
}

void loop() {
  // Wenn der Interrupt die Flagge auf "true" gesetzt hat
  if (buttonPressed) {
    Serial.println("\nKnopf gedrückt registriert!");
    
    // Prüfen, ob das WLAN noch steht
    if (WiFi.status() == WL_CONNECTED) {
      logTimeToJira();
    } else {
      Serial.println("Fehler: Keine WLAN-Verbindung!");
    }
    
    // Flagge zurücksetzen, um auf den nächsten Druck zu warten
    buttonPressed = false;
  }
  
  // Kurzer Delay im Loop schont die CPU
  delay(10);
}

void logTimeToJira() {
  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;
  
  String url = "https://" + String(jiraHost) + "/rest/api/3/issue/" + String(issueKey) + "/worklog";
  
  Serial.println("Sende Request an Jira...");
  http.begin(client, url);
  
  http.addHeader("Authorization", "Basic " + String(base64Credentials));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  String jsonPayload = "{\"timeSpent\": \"1h\", \"comment\": {\"type\": \"doc\", \"version\": 1, \"content\": [{\"type\": \"paragraph\", \"content\": [{\"text\": \"Automatisch gebucht via ESP32 Hardware-Button.\", \"type\": \"text\"}]}]}}";

  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("HTTP Code: ");
    Serial.println(httpResponseCode);
    if (httpResponseCode == 201) {
      Serial.println(">>> Erfolg! 1 Stunde wurde gebucht. <<<");
    } else {
      Serial.println("Fehler beim Buchen. Antwort:");
      Serial.println(response);
    }
  } else {
    Serial.print("Verbindungsfehler: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
}
