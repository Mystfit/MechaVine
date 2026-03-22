#ifndef MECHAVINE_CONFIG_H
#define MECHAVINE_CONFIG_H

#include <Preferences.h>

struct MechaVineConfig {
  uint32_t soundThreshold;     // Spike threshold — instantaneous RMS
  uint32_t sustainedThreshold; // Sustained threshold — slow moving average
  uint32_t quietDelayMs;
  uint16_t servoSpeedOpening;
  uint16_t servoSpeedClosing;
};

const MechaVineConfig DEFAULT_CONFIG = {
  .soundThreshold    = 400000,
  .sustainedThreshold = 200000,
  .quietDelayMs      = 3000,
  .servoSpeedOpening = 60,
  .servoSpeedClosing = 60
};

// NVS delayed-save state
static bool configDirty = false;
static unsigned long configLastChangeTime = 0;
const unsigned long CONFIG_SAVE_DELAY_MS = 2000;

inline void loadConfig(Preferences& prefs, MechaVineConfig& cfg) {
  cfg.soundThreshold     = prefs.getUInt("sndThresh",  DEFAULT_CONFIG.soundThreshold);
  cfg.sustainedThreshold = prefs.getUInt("susThresh",  DEFAULT_CONFIG.sustainedThreshold);
  cfg.quietDelayMs       = prefs.getUInt("quietDelay", DEFAULT_CONFIG.quietDelayMs);
  cfg.servoSpeedOpening  = prefs.getUShort("spdOpen",  DEFAULT_CONFIG.servoSpeedOpening);
  cfg.servoSpeedClosing  = prefs.getUShort("spdClose", DEFAULT_CONFIG.servoSpeedClosing);
}

inline void saveConfigKey(Preferences& prefs, const char* key, uint32_t value) {
  if (strcmp(key, "spdOpen") == 0 || strcmp(key, "spdClose") == 0) {
    prefs.putUShort(key, (uint16_t)value);
  } else {
    prefs.putUInt(key, value);
  }
}

inline void markConfigDirty() {
  configDirty = true;
  configLastChangeTime = millis();
}

inline void checkConfigSave(Preferences& prefs, const MechaVineConfig& cfg) {
  if (configDirty && (millis() - configLastChangeTime >= CONFIG_SAVE_DELAY_MS)) {
    saveConfigKey(prefs, "sndThresh",  cfg.soundThreshold);
    saveConfigKey(prefs, "susThresh",  cfg.sustainedThreshold);
    saveConfigKey(prefs, "quietDelay", cfg.quietDelayMs);
    saveConfigKey(prefs, "spdOpen",    cfg.servoSpeedOpening);
    saveConfigKey(prefs, "spdClose",   cfg.servoSpeedClosing);
    configDirty = false;
    Serial.println("[Config] Saved to NVS");
  }
}

#endif
