/*
 * ESP32-S3 Audio Level Meter with INMP441 Microphone and WS2812B LEDs
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
 * WS2812B LED Chain (3 LEDs):
 *   DIN  (Data Input)    -> GPIO 7
 *   VDD                  -> 5V
 *   GND                  -> GND
 *   Note: LEDs are daisy-chained (DOUT of LED1 -> DIN of LED2 -> DIN of LED3)
 *
 * Behavior:
 *   - Reads audio from INMP441 microphone
 *   - Calculates RMS loudness
 *   - Progressively lights LEDs like a peak meter:
 *     LED1: Green (low level)
 *     LED2: Yellow (medium level)
 *     LED3: Green (high level) -> Red (peak warning)
 */

#include <ESP32_WS2812B_RMT.h>
#include <ESP32_INMP441.h>

// Pin Definitions
const uint8_t MIC_SCK  = 4;
const uint8_t MIC_WS   = 5;
const uint8_t MIC_DIN  = 6;
const uint8_t LED_PIN  = 7;

// Hardware Configuration
#define NUM_LEDS 3

// LED Threshold Levels (adjust these based on your environment)
// These represent RMS amplitude values from 24-bit audio samples
// Max 24-bit signed value is ~8,388,608, so RMS max is around 5,900,000
#define THRESHOLD_LED1  350000   // Low level - First LED
#define THRESHOLD_LED2  500000   // Medium level - Second LED
#define THRESHOLD_LED3  600000   // High level - Third LED
#define PEAK_THRESHOLD  650000   // Peak level - Trigger red color

// Global Objects
WS2812B_RMT leds(NUM_LEDS);
INMP441 mic;
uint8_t ledLevels[NUM_LEDS];

// Function Prototypes
void mapRMSToLEDLevels(float rms);
void updateLEDs(float rms);

void setup() {
  Serial.begin(115200);
  delay(1000);  // Allow serial to initialize

  Serial.println("\n===========================================");
  Serial.println("ESP32-S3 Audio Level Meter");
  Serial.println("===========================================");

  // Initialize I2S microphone
  Serial.println("\n[1/3] Initializing I2S microphone...");
  if (!mic.begin(MIC_SCK, MIC_WS, MIC_DIN)) {
    Serial.println("ERROR: Failed to initialize I2S!");
    Serial.println("Check INMP441 connections:");
    Serial.println("  SCK -> GPIO 4");
    Serial.println("  WS  -> GPIO 5");
    Serial.println("  SD  -> GPIO 6");
    Serial.println("  L/R -> GND");
    Serial.println("  VDD -> 3.3V");
    while(1) { delay(1000); }
  }
  Serial.println("  ✓ I2S initialized at 16kHz, 32-bit, mono");

  // Initialize RMT for WS2812B control
  Serial.println("\n[2/3] Initializing RMT for LEDs...");
  if (!leds.begin(LED_PIN)) {
    Serial.println("ERROR: Failed to initialize RMT!");
    Serial.println("Check WS2812B connection:");
    Serial.println("  DIN -> GPIO 7");
    Serial.println("  VDD -> 5V");
    Serial.println("  GND -> GND");
    while(1) { delay(1000); }
  }
  Serial.println("  ✓ RMT initialized at 10MHz for WS2812B timing");

  // Set all LEDs to off initially
  Serial.println("\n[3/3] Initializing LEDs...");
  leds.clear();
  leds.show();
  Serial.println("  ✓ All LEDs cleared");

  // LED startup test - blink each LED in sequence
  Serial.println("\n[TEST] LED sequence test...");

  // Test LED 1 - Green
  Serial.println("  LED 1: Green");
  leds.setPixel(0, WS2812B_Colors::GREEN);
  leds.show();
  delay(500);
  leds.clear();
  leds.show();

  // Test LED 2 - Yellow
  Serial.println("  LED 2: Yellow");
  leds.setPixel(1, WS2812B_Colors::YELLOW);
  leds.show();
  delay(500);
  leds.clear();
  leds.show();

  // Test LED 3 - Red
  Serial.println("  LED 3: Red");
  leds.setPixel(2, WS2812B_Colors::RED);
  leds.show();
  delay(500);
  leds.clear();
  leds.show();

  Serial.println("  ✓ LED test complete - all off");

  Serial.println("\n===========================================");
  Serial.println("Setup complete. Starting audio processing...");
  Serial.println("===========================================\n");
  Serial.println("Thresholds: LED1=350k, LED2=500k, LED3=600k, PEAK=650k");
  Serial.println("Quiet room RMS should be < 50,000\n");
}

void loop() {
  // Read audio samples and calculate RMS
  if (!mic.read()) return;

  float rms = mic.getRMS();

  // Map RMS to LED levels (0 = off, 255 = full brightness)
  mapRMSToLEDLevels(rms);

  // Update LED colors based on levels
  updateLEDs(rms);

  // Print debug info every ~30 iterations (~1 second at 16kHz/512 buffer)
  static int debugCounter = 0;
  if (++debugCounter >= 30) {
    debugCounter = 0;
    const int32_t* buf = mic.getAudioBuffer();
    Serial.printf("RMS: %12.0f | LEDs: %3d %3d %3d | Samples: %d, %d, %d\n",
                  rms, ledLevels[0], ledLevels[1], ledLevels[2],
                  buf[0], buf[1], buf[2]);
  }
}

/**
 * Map RMS value to LED activation levels
 * Progressive activation: LED1 -> LED2 -> LED3
 * LED3 turns red when exceeding peak threshold
 */
void mapRMSToLEDLevels(float rms) {
  // LED 1 (first in chain) - Progressive activation
  if (rms < THRESHOLD_LED1) {
    ledLevels[0] = 0;  // Off
  } else if (rms < THRESHOLD_LED2) {
    // Fade in from threshold1 to threshold2
    float ratio = (rms - THRESHOLD_LED1) / (THRESHOLD_LED2 - THRESHOLD_LED1);
    ledLevels[0] = (uint8_t)(ratio * 255);
  } else {
    ledLevels[0] = 255;  // Full brightness
  }

  // LED 2 (middle) - Activates after LED1 is partially on
  if (rms < THRESHOLD_LED2) {
    ledLevels[1] = 0;  // Off
  } else if (rms < THRESHOLD_LED3) {
    // Fade in from threshold2 to threshold3
    float ratio = (rms - THRESHOLD_LED2) / (THRESHOLD_LED3 - THRESHOLD_LED2);
    ledLevels[1] = (uint8_t)(ratio * 255);
  } else {
    ledLevels[1] = 255;  // Full brightness
  }

  // LED 3 (last) - Activates last, turns red at peak
  if (rms < THRESHOLD_LED3) {
    ledLevels[2] = 0;  // Off
  } else if (rms < PEAK_THRESHOLD) {
    // Fade in from threshold3 to peak threshold
    float ratio = (rms - THRESHOLD_LED3) / (PEAK_THRESHOLD - THRESHOLD_LED3);
    ledLevels[2] = (uint8_t)(ratio * 255);
  } else {
    ledLevels[2] = 255;  // Full brightness (will be red)
  }
}

/**
 * Update all LEDs based on current levels
 */
void updateLEDs(float rms) {
  // LED 1: Green (scaled by level)
  leds.setPixel(0, WS2812B_Colors::GREEN, ledLevels[0]);

  // LED 2: Yellow/Amber (scaled by level)
  leds.setPixel(1, WS2812B_Colors::YELLOW, ledLevels[1]);

  // LED 3: Green normally, Red when at peak
  if (rms >= PEAK_THRESHOLD) {
    leds.setPixel(2, WS2812B_Colors::RED, ledLevels[2]);
  } else {
    leds.setPixel(2, WS2812B_Colors::GREEN, ledLevels[2]);
  }

  leds.show();
}
