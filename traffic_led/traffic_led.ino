/*
 * =====================================================
 * ESP32 Traffic Light Simulation with Firebase
 * =====================================================
 * Hardware:
 *   - Red LED    → GPIO 4
 *   - Yellow LED → GPIO 5
 *   - Green LED  → GPIO 18
 *   - All LEDs with 220Ω resistors to GND
 *
 * Library: Firebase Arduino Client Library for ESP8266
 *          and ESP32 by Mobizt (v4.4.17)
 * =====================================================
 */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// ─── WiFi Credentials ──────────────────────────────
#define WIFI_SSID     "K3NT0Z4K1"
#define WIFI_PASSWORD "12345678"

// ─── Firebase Credentials ──────────────────────────
// Using Database Secret (legacy token) — most reliable method
#define FIREBASE_DB_URL  "traffic-sim-5a9ef-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SECRET  "7etDsjZimq0GkGepNOJ61hIY9HYvruznDSW0A0kZ"

// ─── LED Pin Definitions ───────────────────────────
#define RED_PIN    4
#define YELLOW_PIN 5
#define GREEN_PIN  18

// ─── Traffic Light Timing (milliseconds) ───────────
#define GREEN_DURATION  15000
#define YELLOW_DURATION 5000
#define RED_DURATION    20000
#define BLINK_INTERVAL  300

// ─── Firebase Objects ───────────────────────────────
FirebaseData   fbData;
FirebaseAuth   auth;
FirebaseConfig config;

// ─── State Variables ───────────────────────────────
enum TrafficState { STATE_RED, STATE_YELLOW, STATE_GREEN, STATE_OFF, STATE_BLINK };

TrafficState  currentState  = STATE_GREEN;
int           currentMode   = 1;
bool          blinkOn       = false;
bool          firebaseReady = false;

unsigned long lastUpdate   = 0;
unsigned long lastFirebase = 0;
unsigned long lastBlink    = 0;
unsigned long phaseStart   = 0;

// ─── Helper: All LEDs OFF ──────────────────────────
void allOff() {
  digitalWrite(RED_PIN,    LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN,  LOW);
}

// ─── Helper: Set specific LEDs ─────────────────────
void setLight(int r, int y, int g) {
  digitalWrite(RED_PIN,    r ? HIGH : LOW);
  digitalWrite(YELLOW_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN_PIN,  g ? HIGH : LOW);
}

// ─── Send State to Firebase ────────────────────────
void sendStateToFirebase(const char* state, int cdSecs) {
  if (!firebaseReady) return;
  if (millis() - lastFirebase < 250) return;
  lastFirebase = millis();

  FirebaseJson json;
  json.set("trafficLightState", state);
  json.set("mode",              currentMode);
  json.set("countdown",         cdSecs);
  json.set("timestamp",         (int)(millis() / 1000));

  if (!Firebase.RTDB.setJSON(&fbData, "/trafficLight", &json)) {
    Serial.print("[Firebase] Write error: ");
    Serial.println(fbData.errorReason());
  } else {
    Serial.printf("[Firebase] ✓ Sent: %s (%ds)\n", state, cdSecs);
  }
}

// ─── Read Mode Command from Firebase ───────────────
void checkFirebaseMode() {
  if (!firebaseReady) return;

  if (Firebase.RTDB.getInt(&fbData, "/trafficLight/mode")) {
    int newMode = fbData.intData();
    if (newMode != currentMode) {
      currentMode = newMode;
      Serial.printf("[Mode] Changed to: %d\n", currentMode);
      phaseStart = millis();
      if      (currentMode == 0) { allOff(); currentState = STATE_OFF; }
      else if (currentMode == 2) { currentState = STATE_BLINK; }
      else                       { currentState = STATE_GREEN; }
    }
  }
}

// ─── Normal Traffic Cycle ──────────────────────────
void runNormalCycle() {
  unsigned long elapsed = millis() - phaseStart;

  switch (currentState) {
    case STATE_GREEN:
      setLight(0, 0, 1);
      sendStateToFirebase("GREEN", max(0L, (long)(GREEN_DURATION - elapsed) / 1000L));
      if (elapsed >= GREEN_DURATION) {
        currentState = STATE_YELLOW;
        phaseStart   = millis();
        Serial.println("[Traffic] GREEN → YELLOW");
      }
      break;

    case STATE_YELLOW:
      setLight(0, 1, 0);
      sendStateToFirebase("YELLOW", max(0L, (long)(YELLOW_DURATION - elapsed) / 1000L));
      if (elapsed >= YELLOW_DURATION) {
        currentState = STATE_RED;
        phaseStart   = millis();
        Serial.println("[Traffic] YELLOW → RED");
      }
      break;

    case STATE_RED:
      setLight(1, 0, 0);
      sendStateToFirebase("RED", max(0L, (long)(RED_DURATION - elapsed) / 1000L));
      if (elapsed >= RED_DURATION) {
        currentState = STATE_GREEN;
        phaseStart   = millis();
        Serial.println("[Traffic] RED → GREEN");
      }
      break;

    default:
      currentState = STATE_GREEN;
      phaseStart   = millis();
      break;
  }
}

// ─── Blink Mode ────────────────────────────────────
void runBlinkMode() {
  if (millis() - lastBlink >= BLINK_INTERVAL) {
    lastBlink = millis();
    blinkOn   = !blinkOn;
    setLight(blinkOn, blinkOn, blinkOn);
  }
  sendStateToFirebase("BLINK", 0);
}

// ─── Setup ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Traffic Light Starting ===");

  // LED pins
  pinMode(RED_PIN,    OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN,  OUTPUT);
  allOff();

  // Quick LED test
  Serial.println("[LED] Testing all LEDs...");
  setLight(1, 0, 0); delay(400);
  setLight(0, 1, 0); delay(400);
  setLight(0, 0, 1); delay(400);
  allOff();

  // Connect WiFi
  Serial.printf("[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int wifiTries = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTries < 30) {
    digitalWrite(RED_PIN, HIGH); delay(300);
    digitalWrite(RED_PIN, LOW);  delay(300);
    Serial.print(".");
    wifiTries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] FAILED to connect! Check SSID/Password.");
    Serial.println("[WiFi] Restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  allOff();

  // ── Firebase Setup using Database Secret (legacy token) ──
  // This method skips token generation — connects instantly!
  config.database_url              = FIREBASE_DB_URL;
  config.signer.tokens.legacy_token = FIREBASE_SECRET;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Short wait then check
  delay(2000);

  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println("[Firebase] ✓ Connected using Database Secret!");
    Firebase.RTDB.setInt(&fbData, "/trafficLight/mode", 1);
  } else {
    Serial.println("[Firebase] Not ready yet — will retry in loop.");
  }

  phaseStart   = millis();
  currentState = STATE_GREEN;
  Serial.println("[Traffic] Cycle starting: GREEN → YELLOW → RED\n");
}

// ─── Main Loop ─────────────────────────────────────
void loop() {
  // Keep retrying Firebase if not connected yet
  if (!firebaseReady && Firebase.ready()) {
    firebaseReady = true;
    Serial.println("[Firebase] ✓ Now connected!");
    Firebase.RTDB.setInt(&fbData, "/trafficLight/mode", 1);
  }

  // Poll Firebase for mode changes every 500ms
  if (millis() - lastUpdate >= 500) {
    lastUpdate = millis();
    checkFirebaseMode();
  }

  // Run current mode
  switch (currentMode) {
    case 0:
      allOff();
      sendStateToFirebase("OFF", 0);
      delay(1000);
      break;
    case 1:
      runNormalCycle();
      break;
    case 2:
      runBlinkMode();
      break;
  }

  delay(50);
}
