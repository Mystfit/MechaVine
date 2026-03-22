/*
 * ESP32-S3 "Shy Flower" — Sound-Reactive Servo with LED Feedback
 *
 * Hardware Connections:
 *
 * INMP441 I2S Microphone:
 *   SCK  (Bit Clock)     -> GPIO 4
 *   WS   (Word Select)   -> GPIO 5
 *   SD   (Serial Data)   -> GPIO 6
 *   L/R  (Channel)       -> GND (for LEFT channel)
 *   VDD                  -> 3.3V
 *   GND                  -> GND
 *
 * WS2812B LED (1 LED):
 *   DIN  (Data Input)    -> GPIO 7
 *   VDD                  -> 5V
 *   GND                  -> GND
 *
 * PCA9685 Servo Breakout (I2C):
 *   SDA                  -> Default I2C SDA
 *   SCL                  -> Default I2C SCL
 *   VDD                  -> 3.3V (logic) / 5-6V (servo power)
 *   GND                  -> GND
 *   Servo on channel 15
 *
 * Behavior:
 *   - Flower starts open (servo at 0°), LED is red
 *   - When sound exceeds threshold, flower closes (servo eases to 180°),
 *     LED lerps from red to purple
 *   - After quiet delay of silence, flower opens again (servo eases to 0°),
 *     LED lerps from purple back to red
 *   - Sound during opening interrupts and triggers closing again
 *
 * Web Calibration:
 *   - Connect to WiFi AP "Mechavine_XX" (password: CubaDupa26)
 *   - Open 192.168.4.1 in browser
 *   - Adjust threshold, quiet delay, and servo speeds via sliders
 *   - Settings persist across power cycles (NVS)
 */

#define USE_PCA9685_SERVO_EXPANDER
#define MAX_EASING_SERVOS 1
#include "ServoEasing.hpp"

#include <ESP32_WS2812B_RMT.h>
#include <ESP32_INMP441.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>

#include "config.h"
#include "web_page.h"

// ── Pin Definitions ──────────────────────────────────────────────
const uint8_t MIC_SCK  = 4;
const uint8_t MIC_WS   = 5;
const uint8_t MIC_DIN  = 6;
const uint8_t LED_PIN  = 7;

// ── Configuration (compile-time constants) ──────────────────────
#define NUM_LEDS          1
#define SERVO_CHANNEL     15        // PCA9685 channel (0-15)
#define SERVO_OPEN_DEG    0         // Degrees when flower is open
#define SERVO_CLOSED_DEG  180       // Degrees when flower is closed

// ── Network Configuration ───────────────────────────────────────
const uint8_t VINE_ID = 1;         // Suffix for SSID: "Mechavine_01"
const char* AP_PASSWORD = "CubaDupa26";

// ── State Machine ────────────────────────────────────────────────
enum FlowerState {
  STATE_OPEN,
  STATE_CLOSING,
  STATE_CLOSED,
  STATE_OPENING
};

// ── Global Objects ───────────────────────────────────────────────
ServoEasing servo(PCA9685_DEFAULT_ADDRESS, &Wire);
WS2812B_RMT leds(NUM_LEDS);
INMP441 mic;
WebServer server(80);
WebSocketsServer webSocket(81);
DNSServer dnsServer;
Preferences preferences;
MechaVineConfig cfg;

FlowerState state = STATE_OPEN;
unsigned long quietStartTime = 0;

// Slow exponential moving average of RMS (~3s time constant at ~30Hz loop rate)
// Used to detect sustained ambient noise, not just sharp spikes.
static const float SLOW_RMS_ALPHA = 0.99f;
float slowRMS = 0.0f;

// ── Color Constants ──────────────────────────────────────────────
// Open = Red (0xFF, 0x00, 0x00), Closed = Purple (0x80, 0x00, 0x80)
const uint8_t COLOR_OPEN_R  = 0xFF, COLOR_OPEN_G  = 0x00, COLOR_OPEN_B  = 0x00;
const uint8_t COLOR_CLOSE_R = 0x80, COLOR_CLOSE_G = 0x00, COLOR_CLOSE_B = 0x80;

// ── Function Prototypes ─────────────────────────────────────────
void updateLEDFromAngle(int angle);
const char* stateName(FlowerState s);
void sendConfig(uint8_t clientNum);
void broadcastConfig();
void broadcastRMS(float rms, float slowRms);
void handleWSMessage(uint8_t num, uint8_t* payload, size_t length);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n===========================================");
  Serial.println("Shy Flower — Sound-Reactive Servo");
  Serial.println("===========================================");

  // ── Load Configuration from NVS ──
  Serial.println("\n[0/5] Loading configuration...");
  preferences.begin("mechavine", false);
  loadConfig(preferences, cfg);
  Serial.printf("  Threshold: %u, Sustained: %u, Quiet: %u ms, Speed Open: %u, Speed Close: %u\n",
                cfg.soundThreshold, cfg.sustainedThreshold, cfg.quietDelayMs, cfg.servoSpeedOpening, cfg.servoSpeedClosing);

  // ── Initialize I2S Microphone ──
  Serial.println("\n[1/5] Initializing I2S microphone...");
  if (!mic.begin(MIC_SCK, MIC_WS, MIC_DIN)) {
    Serial.println("ERROR: Failed to initialize I2S!");
    Serial.println("Check INMP441 connections:");
    Serial.println("  SCK -> GPIO 4");
    Serial.println("  WS  -> GPIO 5");
    Serial.println("  SD  -> GPIO 6");
    Serial.println("  L/R -> GND");
    Serial.println("  VDD -> 3.3V");
    while (1) { delay(1000); }
  }
  Serial.println("  OK — I2S initialized");

  // ── Initialize WS2812B LED ──
  Serial.println("\n[2/5] Initializing LED...");
  if (!leds.begin(LED_PIN)) {
    Serial.println("ERROR: Failed to initialize RMT!");
    Serial.println("Check WS2812B connection:");
    Serial.println("  DIN -> GPIO 7");
    while (1) { delay(1000); }
  }
  leds.clear();
  leds.show();
  Serial.println("  OK — LED initialized");

  // ── Initialize PCA9685 + Servo ──
  Serial.println("\n[3/5] Initializing PCA9685 servo...");
  if (servo.InitializeAndCheckI2CConnection(&Serial)) {
    Serial.println("ERROR: PCA9685 not found on I2C bus!");
    Serial.println("Check I2C connections (SDA/SCL) and PCA9685 power.");
    while (1) { delay(1000); }
  }
  if (servo.attach(SERVO_CHANNEL, SERVO_OPEN_DEG) == INVALID_SERVO) {
    Serial.println("ERROR: Failed to attach servo to channel!");
    while (1) { delay(1000); }
  }
  servo.setEasingType(EASE_CUBIC_IN_OUT);
  servo.setSpeed(cfg.servoSpeedClosing);
  Serial.printf("  OK — Servo on PCA9685 channel %d, start %d deg\n", SERVO_CHANNEL, SERVO_OPEN_DEG);

  // ── Set initial LED to red (open state) ──
  leds.setPixel(0, WS2812B_Colors::RED);
  leds.show();

  // ── Initialize WiFi Soft AP ──
  Serial.println("\n[4/5] Starting WiFi AP...");
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "Mechavine_%02d", VINE_ID);
  WiFi.softAP(ssid, AP_PASSWORD);
  delay(100);
  Serial.printf("  OK — SSID: %s, IP: %s\n", ssid, WiFi.softAPIP().toString().c_str());

  // ── Initialize Web Server + WebSocket ──
  Serial.println("\n[5/5] Starting web server...");
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", []() {
    server.send_P(200, "text/html", WEB_PAGE);
  });
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
  });
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("  OK — Web server on port 80, WebSocket on port 81");

  Serial.println("\n===========================================");
  Serial.println("Setup complete. Flower is OPEN.");
  Serial.printf("Threshold: %u RMS, Quiet delay: %u ms\n", cfg.soundThreshold, cfg.quietDelayMs);
  Serial.printf("Servo speed: open %u, close %u deg/s\n", cfg.servoSpeedOpening, cfg.servoSpeedClosing);
  Serial.println("===========================================\n");
}

void loop() {
  // ── Service network ──
  dnsServer.processNextRequest();
  webSocket.loop();
  server.handleClient();

  // ── Delayed NVS save ──
  checkConfigSave(preferences, cfg);

  // ── Read microphone ──
  if (!mic.read()) return;
  float rms = mic.getRMS();
  slowRMS = SLOW_RMS_ALPHA * slowRMS + (1.0f - SLOW_RMS_ALPHA) * rms;

  bool loud      = rms     > cfg.soundThreshold;      // fast path: spike
  bool sustained = slowRMS > cfg.sustainedThreshold;   // slow path: ambient

  FlowerState prevState = state;

  switch (state) {
    case STATE_OPEN:
      if (loud || sustained) {
        servo.startEaseTo(SERVO_CLOSED_DEG, cfg.servoSpeedClosing);
        state = STATE_CLOSING;
      }
      break;

    case STATE_CLOSING:
      updateLEDFromAngle(servo.getCurrentAngle());
      if (!servo.isMoving()) {
        state = STATE_CLOSED;
        quietStartTime = millis();
      }
      break;

    case STATE_CLOSED:
      // Keep LED purple
      leds.setPixel(0, (COLOR_CLOSE_R << 16) | (COLOR_CLOSE_G << 8) | COLOR_CLOSE_B);
      leds.show();
      if (loud || sustained) {
        // Reset quiet timer (both spike and sustained ambient keep it closed)
        quietStartTime = millis();
      } else if (millis() - quietStartTime >= cfg.quietDelayMs) {
        servo.startEaseTo(SERVO_OPEN_DEG, cfg.servoSpeedOpening);
        state = STATE_OPENING;
      }
      break;

    case STATE_OPENING:
      updateLEDFromAngle(servo.getCurrentAngle());
      if (loud || sustained) {
        // Interrupt opening — close again
        servo.startEaseTo(SERVO_CLOSED_DEG, cfg.servoSpeedClosing);
        state = STATE_CLOSING;
      } else if (!servo.isMoving()) {
        state = STATE_OPEN;
        // Ensure LED is fully red
        leds.setPixel(0, WS2812B_Colors::RED);
        leds.show();
      }
      break;
  }

  // Log state transitions
  if (state != prevState) {
    Serial.printf("State: %s -> %s  (RMS: %.0f, slowRMS: %.0f)\n",
                  stateName(prevState), stateName(state), rms, slowRMS);
  }

  // ── Broadcast RMS via WebSocket (~10Hz) ──
  static uint8_t wsCounter = 0;
  if (++wsCounter >= 3) {
    wsCounter = 0;
    broadcastRMS(rms, slowRMS);
  }

  // Periodic debug output
  static int debugCounter = 0;
  if (++debugCounter >= 30) {
    debugCounter = 0;
    Serial.printf("RMS: %8.0f | slowRMS: %8.0f | State: %-8s | Angle: %d\n",
                  rms, slowRMS, stateName(state), servo.getCurrentAngle());
  }
}

// ── WebSocket Event Handler ─────────────────────────────────────

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] Client %u connected\n", num);
      sendConfig(num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client %u disconnected\n", num);
      break;
    case WStype_TEXT:
      handleWSMessage(num, payload, length);
      break;
    default:
      break;
  }
}

void sendConfig(uint8_t clientNum) {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"cfg\":{\"threshold\":%u,\"sustainedThreshold\":%u,\"quietDelay\":%u,\"speedOpen\":%u,\"speedClose\":%u}}",
    cfg.soundThreshold, cfg.sustainedThreshold, cfg.quietDelayMs, cfg.servoSpeedOpening, cfg.servoSpeedClosing);
  webSocket.sendTXT(clientNum, buf);
}

void broadcastConfig() {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"cfg\":{\"threshold\":%u,\"sustainedThreshold\":%u,\"quietDelay\":%u,\"speedOpen\":%u,\"speedClose\":%u}}",
    cfg.soundThreshold, cfg.sustainedThreshold, cfg.quietDelayMs, cfg.servoSpeedOpening, cfg.servoSpeedClosing);
  webSocket.broadcastTXT(buf);
}

void broadcastRMS(float rms, float slowRms) {
  char buf[100];
  snprintf(buf, sizeof(buf), "{\"rms\":%.0f,\"slowRms\":%.0f,\"state\":\"%s\"}",
           rms, slowRms, stateName(state));
  webSocket.broadcastTXT(buf);
}

void handleWSMessage(uint8_t num, uint8_t* payload, size_t length) {
  // Parse simple JSON: {"set":"threshold","val":500000}
  char* msg = (char*)payload;

  char* setKey = strstr(msg, "\"set\":\"");
  char* valStr = strstr(msg, "\"val\":");
  if (!setKey || !valStr) return;

  setKey += 7; // skip past "set":"
  char* keyEnd = strchr(setKey, '"');
  if (!keyEnd) return;
  *keyEnd = '\0';

  valStr += 6; // skip past "val":
  uint32_t val = strtoul(valStr, NULL, 10);

  if (strcmp(setKey, "threshold") == 0) {
    cfg.soundThreshold = val;
    Serial.printf("[WS] threshold = %u\n", val);
  } else if (strcmp(setKey, "sustainedThreshold") == 0) {
    cfg.sustainedThreshold = val;
    Serial.printf("[WS] sustainedThreshold = %u\n", val);
  } else if (strcmp(setKey, "quietDelay") == 0) {
    cfg.quietDelayMs = val;
    Serial.printf("[WS] quietDelay = %u\n", val);
  } else if (strcmp(setKey, "speedOpen") == 0) {
    cfg.servoSpeedOpening = (uint16_t)val;
    Serial.printf("[WS] speedOpen = %u\n", val);
  } else if (strcmp(setKey, "speedClose") == 0) {
    cfg.servoSpeedClosing = (uint16_t)val;
    Serial.printf("[WS] speedClose = %u\n", val);
  } else {
    return;
  }

  markConfigDirty();
  broadcastConfig();
}

// ── LED + State Helpers ─────────────────────────────────────────

/**
 * Interpolate LED color between red (0°) and purple (180°)
 * based on current servo angle.
 */
void updateLEDFromAngle(int angle) {
  float t = constrain((float)angle / (float)SERVO_CLOSED_DEG, 0.0f, 1.0f);
  uint8_t r = COLOR_OPEN_R + (int)((COLOR_CLOSE_R - COLOR_OPEN_R) * t);
  uint8_t g = COLOR_OPEN_G + (int)((COLOR_CLOSE_G - COLOR_OPEN_G) * t);
  uint8_t b = COLOR_OPEN_B + (int)((COLOR_CLOSE_B - COLOR_OPEN_B) * t);
  uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
  leds.setPixel(0, color);
  leds.show();
}

/** Return a human-readable name for the current state. */
const char* stateName(FlowerState s) {
  switch (s) {
    case STATE_OPEN:    return "OPEN";
    case STATE_CLOSING: return "CLOSING";
    case STATE_CLOSED:  return "CLOSED";
    case STATE_OPENING: return "OPENING";
    default:            return "UNKNOWN";
  }
}
