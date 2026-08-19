#include <WiFi.h>
#include <HTTPClient.h>

#if __has_include("secrets.h")
  #include "secrets.h"
  #define LOVEVOICE_HAS_SECRETS 1
#else
  #define LOVEVOICE_HAS_SECRETS 0
#endif

/*
  Love Voice — A Test: Physical Interaction
  Board: Arduino Nano ESP32

  Tests:
  - Heart button input
  - Hold-to-Speak gesture
  - Pull-to-adjust volume
  - DAILY / SOS physical state
  - 2-second SOS confirmation hold
  - Optional Wi-Fi event logging

  Prototype only — not emergency-critical software.
*/

// -----------------------------------------------------------------------------
// Pin mapping — change to match your breadboard
// -----------------------------------------------------------------------------
const uint8_t HEART_BUTTON_PIN    = 2;
const uint8_t SPEAK_BUTTON_PIN    = 3;
const uint8_t SAFETY_SELECTOR_PIN = 4;   // HIGH = DAILY, LOW = SOS
const uint8_t VOLUME_SENSOR_PIN   = A0;  // B10K linear potentiometer
const uint8_t STATUS_LED_PIN      = LED_BUILTIN;

// -----------------------------------------------------------------------------
// Interaction timing
// -----------------------------------------------------------------------------
const uint32_t DEBOUNCE_MS            = 35;
const uint32_t HOLD_TO_SPEAK_MS       = 350;
const uint32_t SOS_CONFIRM_HOLD_MS    = 2000;
const uint32_t VOLUME_SAMPLE_MS       = 100;
const uint32_t VOLUME_REPORT_MIN_MS   = 700;
const int      VOLUME_CHANGE_THRESHOLD = 4;  // percentage points

// -----------------------------------------------------------------------------
// Device state
// -----------------------------------------------------------------------------
enum DeviceMode {
  MODE_DAILY,
  MODE_SOS
};

DeviceMode currentMode = MODE_DAILY;

struct DebouncedInput {
  uint8_t pin;
  bool stableState = HIGH;
  bool lastRawState = HIGH;
  uint32_t rawChangedAt = 0;
  bool fell = false;
  bool rose = false;

  void begin(uint8_t inputPin) {
    pin = inputPin;
    pinMode(pin, INPUT_PULLUP);
    stableState = digitalRead(pin);
    lastRawState = stableState;
    rawChangedAt = millis();
  }

  void update() {
    fell = false;
    rose = false;

    bool raw = digitalRead(pin);
    uint32_t now = millis();

    if (raw != lastRawState) {
      lastRawState = raw;
      rawChangedAt = now;
    }

    if ((now - rawChangedAt) >= DEBOUNCE_MS && raw != stableState) {
      bool previous = stableState;
      stableState = raw;
      fell = (previous == HIGH && stableState == LOW);
      rose = (previous == LOW && stableState == HIGH);
    }
  }

  bool active() const {
    return stableState == LOW;
  }
};

DebouncedInput heartButton;
DebouncedInput speakButton;
DebouncedInput safetySelector;

uint32_t speakPressedAt = 0;
DeviceMode modeAtSpeakPress = MODE_DAILY;
bool dailyHoldAccepted = false;
bool sosHoldAccepted = false;

uint32_t lastVolumeSampleAt = 0;
uint32_t lastVolumeReportAt = 0;
int currentVolumePercent = 50;
int lastReportedVolumePercent = -100;

// -----------------------------------------------------------------------------
// Optional connected event logging
// -----------------------------------------------------------------------------
void connectWiFiIfConfigured() {
#if LOVEVOICE_HAS_SECRETS
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WiFi] No SSID configured; running offline.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[WiFi] Connecting");
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Offline mode; physical tests still work.");
  }
#else
  Serial.println("[WiFi] secrets.h not found; running offline.");
#endif
}

void uploadEvent(const String& type, const String& content) {
  Serial.print("[EVENT] ");
  Serial.print(type);
  Serial.print(" | ");
  Serial.println(content);

#if LOVEVOICE_HAS_SECRETS
  if (WiFi.status() != WL_CONNECTED || strlen(SERVER_URL) == 0) {
    return;
  }

  HTTPClient http;
  http.setTimeout(3000);
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"type\":\"" + type + "\",\"content\":\"" + content + "\"}";
  int code = http.POST(payload);

  Serial.print("[HTTP] status = ");
  Serial.println(code);
  http.end();
#endif
}

// -----------------------------------------------------------------------------
// A1. DAILY / SOS state logic
// -----------------------------------------------------------------------------
void updateSafetyMode() {
  safetySelector.update();

  if (safetySelector.fell) {
    currentMode = MODE_SOS;
    digitalWrite(STATUS_LED_PIN, HIGH);
    uploadEvent("mode_change", "SOS");
  }

  if (safetySelector.rose) {
    currentMode = MODE_DAILY;
    digitalWrite(STATUS_LED_PIN, LOW);
    uploadEvent("mode_change", "DAILY");
  }
}

// -----------------------------------------------------------------------------
// A2. Heart button
// -----------------------------------------------------------------------------
void updateHeartButton() {
  heartButton.update();

  if (heartButton.fell) {
    uploadEvent("family_message_request", "Heart button pressed");
  }
}

// -----------------------------------------------------------------------------
// A3. Hold-to-Speak / SOS long-hold
// -----------------------------------------------------------------------------
void updateSpeakButton() {
  speakButton.update();
  uint32_t now = millis();

  if (speakButton.fell) {
    speakPressedAt = now;
    modeAtSpeakPress = currentMode;
    dailyHoldAccepted = false;
    sosHoldAccepted = false;
  }

  if (speakButton.active()) {
    uint32_t heldFor = now - speakPressedAt;

    if (modeAtSpeakPress == MODE_DAILY &&
        !dailyHoldAccepted &&
        heldFor >= HOLD_TO_SPEAK_MS) {
      dailyHoldAccepted = true;
      uploadEvent("voice_reply_start", "Hold-to-Speak accepted in DAILY mode");
      // Integration hook: start microphone capture here.
    }

    if (modeAtSpeakPress == MODE_SOS &&
        !sosHoldAccepted &&
        heldFor >= SOS_CONFIRM_HOLD_MS) {
      sosHoldAccepted = true;
      uploadEvent("sos_confirmed", "SOS confirmed after 2-second hold");
      // Integration hook: notify authorised family endpoint here.
    }
  }

  if (speakButton.rose) {
    uint32_t heldFor = now - speakPressedAt;

    if (modeAtSpeakPress == MODE_DAILY) {
      if (dailyHoldAccepted) {
        uploadEvent("voice_reply_end", "Hold released; voice reply interaction ended");
        // Integration hook: stop microphone capture / upload voice here.
      } else {
        uploadEvent("voice_reply_cancelled", "Press was too short: " + String(heldFor) + " ms");
      }
    } else {
      if (!sosHoldAccepted) {
        uploadEvent("sos_cancelled", "Released before confirmation: " + String(heldFor) + " ms");
      }
    }
  }
}

// -----------------------------------------------------------------------------
// A4. Pull-to-adjust volume
// -----------------------------------------------------------------------------
void updatePullVolume() {
  uint32_t now = millis();
  if (now - lastVolumeSampleAt < VOLUME_SAMPLE_MS) {
    return;
  }
  lastVolumeSampleAt = now;

  int raw = analogRead(VOLUME_SENSOR_PIN);  // 0..4095 at 12-bit resolution
  int mapped = map(raw, 0, 4095, 0, 100);
  currentVolumePercent = constrain(mapped, 0, 100);

  bool changedEnough = abs(currentVolumePercent - lastReportedVolumePercent) >= VOLUME_CHANGE_THRESHOLD;
  bool reportWindowOpen = (now - lastVolumeReportAt) >= VOLUME_REPORT_MIN_MS;

  if (changedEnough && reportWindowOpen) {
    lastReportedVolumePercent = currentVolumePercent;
    lastVolumeReportAt = now;
    uploadEvent("volume_change", String(currentVolumePercent) + "%");

    // Integration hook: map this value to I2S / amplifier output gain.
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=== Love Voice | A Test: Physical Interaction ===");

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  analogReadResolution(12);

  heartButton.begin(HEART_BUTTON_PIN);
  speakButton.begin(SPEAK_BUTTON_PIN);
  safetySelector.begin(SAFETY_SELECTOR_PIN);

  currentMode = safetySelector.active() ? MODE_SOS : MODE_DAILY;
  digitalWrite(STATUS_LED_PIN, currentMode == MODE_SOS ? HIGH : LOW);

  connectWiFiIfConfigured();

  uploadEvent(
    "prototype_start",
    currentMode == MODE_SOS ? "Started in SOS mode" : "Started in DAILY mode"
  );
}

void loop() {
  updateSafetyMode();
  updateHeartButton();
  updateSpeakButton();
  updatePullVolume();
  delay(5);
}
