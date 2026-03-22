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
 *   - After 3 seconds of silence, flower opens again (servo eases to 0°),
 *     LED lerps from purple back to red
 *   - Sound during opening interrupts and triggers closing again
 */

#define USE_PCA9685_SERVO_EXPANDER
#define MAX_EASING_SERVOS 1
#include "ServoEasing.hpp"

#include <ESP32_WS2812B_RMT.h>
#include <ESP32_INMP441.h>

// ── Pin Definitions ──────────────────────────────────────────────
const uint8_t MIC_SCK  = 4;
const uint8_t MIC_WS   = 5;
const uint8_t MIC_DIN  = 6;
const uint8_t LED_PIN  = 7;

// ── Configuration ────────────────────────────────────────────────
#define NUM_LEDS          1
#define SERVO_CHANNEL     15        // PCA9685 channel (0-15)
#define SERVO_OPEN_DEG    0         // Degrees when flower is open
#define SERVO_CLOSED_DEG  180       // Degrees when flower is closed
#define SERVO_SPEED       60        // Degrees per second for easing
#define SOUND_THRESHOLD   400000    // RMS threshold to trigger closing
#define QUIET_DELAY_MS    3000      // Milliseconds of silence before opening

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

FlowerState state = STATE_OPEN;
unsigned long quietStartTime = 0;

// ── Color Constants ──────────────────────────────────────────────
// Open = Red (0xFF, 0x00, 0x00), Closed = Purple (0x80, 0x00, 0x80)
const uint8_t COLOR_OPEN_R  = 0xFF, COLOR_OPEN_G  = 0x00, COLOR_OPEN_B  = 0x00;
const uint8_t COLOR_CLOSE_R = 0x80, COLOR_CLOSE_G = 0x00, COLOR_CLOSE_B = 0x80;

// ── Function Prototypes ─────────────────────────────────────────
void updateLEDFromAngle(int angle);
const char* stateName(FlowerState s);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n===========================================");
  Serial.println("Shy Flower — Sound-Reactive Servo");
  Serial.println("===========================================");

  // ── Initialize I2S Microphone ──
  Serial.println("\n[1/3] Initializing I2S microphone...");
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
  Serial.println("\n[2/3] Initializing LED...");
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
  Serial.println("\n[3/3] Initializing PCA9685 servo...");
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
  servo.setSpeed(SERVO_SPEED);
  Serial.printf("  OK — Servo on PCA9685 channel %d, start %d deg\n", SERVO_CHANNEL, SERVO_OPEN_DEG);

  // ── Set initial LED to red (open state) ──
  leds.setPixel(0, WS2812B_Colors::RED);
  leds.show();

  Serial.println("\n===========================================");
  Serial.println("Setup complete. Flower is OPEN.");
  Serial.printf("Threshold: %d RMS, Quiet delay: %d ms\n", SOUND_THRESHOLD, QUIET_DELAY_MS);
  Serial.println("===========================================\n");
}

void loop() {
  // Read microphone
  if (!mic.read()) return;
  float rms = mic.getRMS();
  bool loud = rms > SOUND_THRESHOLD;

  FlowerState prevState = state;

  switch (state) {
    case STATE_OPEN:
      if (loud) {
        servo.startEaseTo(SERVO_CLOSED_DEG, SERVO_SPEED);
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
      if (loud) {
        // Reset quiet timer
        quietStartTime = millis();
      } else if (millis() - quietStartTime >= QUIET_DELAY_MS) {
        servo.startEaseTo(SERVO_OPEN_DEG, SERVO_SPEED);
        state = STATE_OPENING;
      }
      break;

    case STATE_OPENING:
      updateLEDFromAngle(servo.getCurrentAngle());
      if (loud) {
        // Interrupt opening — close again
        servo.startEaseTo(SERVO_CLOSED_DEG, SERVO_SPEED);
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
    Serial.printf("State: %s -> %s  (RMS: %.0f)\n", stateName(prevState), stateName(state), rms);
  }

  // Periodic debug output
  static int debugCounter = 0;
  if (++debugCounter >= 30) {
    debugCounter = 0;
    Serial.printf("RMS: %8.0f | State: %-8s | Angle: %d\n",
                  rms, stateName(state), servo.getCurrentAngle());
  }
}

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
