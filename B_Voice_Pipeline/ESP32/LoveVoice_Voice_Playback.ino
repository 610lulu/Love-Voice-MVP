#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <ESP_I2S.h>
#include "secrets.h"

/*
  Love Voice — B Test: Voice Pipeline Playback
  Board: Arduino Nano ESP32
  Output: MAX98357A I2S amplifier -> speaker

  Pipeline under test:
  consented family voice sample -> voice clone -> TTS -> pcm_16000
  -> HTTP download -> ESP32 -> I2S -> MAX98357A -> speaker

  This sketch does NOT perform voice cloning on the ESP32.
  It only validates the final connected playback stage.
*/

// -----------------------------------------------------------------------------
// Change these pins to match your breadboard wiring.
// MAX98357A: BCLK, LRC/WS, DIN
// -----------------------------------------------------------------------------
const uint8_t I2S_BCLK_PIN = 5;
const uint8_t I2S_WS_PIN   = 6;
const uint8_t I2S_DOUT_PIN = 7;

const uint8_t PLAY_BUTTON_PIN = 2;
const uint8_t STATUS_LED_PIN  = LED_BUILTIN;

const uint32_t BUTTON_DEBOUNCE_MS = 40;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

const uint32_t AUDIO_SAMPLE_RATE = 16000;

I2SClass I2S;

bool stableButtonState = HIGH;
bool lastRawButtonState = HIGH;
uint32_t buttonRawChangedAt = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[WiFi] Connecting");
  uint32_t started = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Connection timeout.");
  }
}

bool beginI2S() {
  I2S.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);

  bool ok = I2S.begin(
    I2S_MODE_STD,
    AUDIO_SAMPLE_RATE,
    I2S_DATA_BIT_WIDTH_16BIT,
    I2S_SLOT_MODE_MONO
  );

  if (!ok) {
    Serial.println("[I2S] Failed to initialise.");
    return false;
  }

  Serial.println("[I2S] 16 kHz / 16-bit / mono output ready.");
  return true;
}

bool playLatestPcm() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[PLAY] Wi-Fi disconnected; reconnecting...");
    connectWiFi();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[PLAY] Cannot start without Wi-Fi.");
    return false;
  }

  NetworkClient client;
  HTTPClient http;
  http.setTimeout(7000);

  if (!http.begin(client, AUDIO_URL)) {
    Serial.println("[HTTP] Failed to begin request.");
    return false;
  }

  Serial.print("[HTTP] GET ");
  Serial.println(AUDIO_URL);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.print("[HTTP] GET failed, status = ");
    Serial.println(code);
    http.end();
    return false;
  }

  int remaining = http.getSize();  // -1 when content length is unknown
  NetworkClient *stream = http.getStreamPtr();

  if (stream == nullptr) {
    Serial.println("[HTTP] No response stream.");
    http.end();
    return false;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);
  Serial.println("[PLAY] Streaming PCM to I2S...");

  // 16-bit PCM must be written in 2-byte sample-aligned chunks.
  uint8_t buffer[1025];
  size_t carry = 0;
  size_t totalAudioBytes = 0;

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    size_t availableBytes = stream->available();

    if (availableBytes > 0) {
      size_t capacity = 1024 - carry;
      size_t toRead = availableBytes < capacity ? availableBytes : capacity;

      int bytesRead = stream->readBytes(buffer + carry, toRead);
      if (bytesRead > 0) {
        if (remaining > 0) {
          remaining -= bytesRead;
        }

        size_t totalBuffered = carry + static_cast<size_t>(bytesRead);
        size_t alignedBytes = totalBuffered & ~static_cast<size_t>(1);

        if (alignedBytes > 0) {
          size_t written = I2S.write(buffer, alignedBytes);
          totalAudioBytes += written;

          if (written != alignedBytes) {
            Serial.print("[I2S] Short write: ");
            Serial.print(written);
            Serial.print(" / ");
            Serial.println(alignedBytes);
          }
        }

        carry = totalBuffered - alignedBytes;
        if (carry == 1) {
          buffer[0] = buffer[alignedBytes];
        }
      }
    }

    delay(1);
  }

  digitalWrite(STATUS_LED_PIN, LOW);
  http.end();

  if (carry != 0) {
    Serial.println("[PLAY] Warning: ignored one trailing unaligned PCM byte.");
  }

  Serial.print("[PLAY] Finished. I2S bytes written: ");
  Serial.println(totalAudioBytes);
  return totalAudioBytes > 0;
}

bool playbackButtonPressed() {
  bool raw = digitalRead(PLAY_BUTTON_PIN);
  uint32_t now = millis();

  if (raw != lastRawButtonState) {
    lastRawButtonState = raw;
    buttonRawChangedAt = now;
  }

  if ((now - buttonRawChangedAt) >= BUTTON_DEBOUNCE_MS &&
      raw != stableButtonState) {
    bool previous = stableButtonState;
    stableButtonState = raw;

    return previous == HIGH && stableButtonState == LOW;
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=== Love Voice | B Test: Voice Pipeline Playback ===");
  Serial.println("Playback must be identified as AI-generated familiar speech.");

  pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  stableButtonState = digitalRead(PLAY_BUTTON_PIN);
  lastRawButtonState = stableButtonState;

  connectWiFi();
  beginI2S();

  Serial.println("[READY] Generate /tts on the Python server, then press PLAY.");
}

void loop() {
  if (playbackButtonPressed()) {
    playLatestPcm();
  }
  delay(5);
}
