// Config.cpp
#include "Config.h"

static const uint8_t kDefaultDebugLevels[DEBUG_CHANNEL_COUNT] = {
  DBG_ERROR, // SYSTEM
  DBG_ERROR, // WEB
  DBG_ERROR, // NETWORK
  DBG_ERROR, // OSC
  DBG_ERROR, // I2C_BUS
  DBG_ERROR, // FADER_CORE
  DBG_OFF,   // FADER_POSITION
  DBG_ERROR, // TOUCH_CORE
  DBG_OFF,   // TOUCH_RAW
  DBG_ERROR, // CALIBRATION
  DBG_ERROR, // EEPROM
  DBG_ERROR, // LED_EXEC
  DBG_ERROR  // OLED
};

//================================
// MAIN FADER ARRAY
//================================
// Holds runtime data for each motorized fader.
// This is the primary data structure manipulated by control logic.
Fader faders[NUM_FADERS];

//================================
// PIN CONFIGURATION ARRAYS
//================================
// Analog input pins connected to fader position sensors (wipers).
const uint8_t ANALOG_PINS[NUM_FADERS] = {14, 15, 16, 17, 20, 21, 22, 23, 24, 25};

// PWM output pins used to control fader motor speed via motor drivers.
const uint8_t PWM_PINS[NUM_FADERS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

//USE 12 for NEOPIXEL
//USE 41 for MRP121/MTCH2120 IRQ
//USE 18 (SDA), 19(SCL) for I2C

// Motor direction control pins — each fader has two pins for H-bridge control.
const uint8_t DIR_PINS1[NUM_FADERS] = {26, 28, 30, 32, 34, 36, 38, 40, 10, 51};
const uint8_t DIR_PINS2[NUM_FADERS] = {27, 29, 31, 33, 35, 37, 39, 13, 11, 52};

// Unique OSC IDs for each fader (used to construct OSC address strings).
const uint16_t OSC_IDS[NUM_FADERS] = {201, 202, 203, 204, 205, 206, 207, 208, 209, 210};

//================================
// FADER BEHAVIOR CONFIGURATION
//================================
// Default PID and motor tuning settings shared across all faders.
FaderConfig Fconfig = {
  .minPwm = MIN_PWM,
  .maxPwm = MAX_PWM,
  .calibratePwm = CALIB_PWM,
  .targetTolerance = TARGET_TOLERANCE,
  .sendTolerance = SEND_TOLERANCE,
  .slowZone = SLOW_ZONE,
  .fastZone = FAST_ZONE,
  .baseBrightness = 5,
  .touchedBrightness = 40,
  .fadeTime = 500,
  .serialDebug = debugMode,
  .sendKeystrokes = false,
  .useLevelPixels = false,
  .allowFaderOscWithoutTouch = true,
  .debugConfigVersion = DEBUG_CONFIG_VERSION,
  .debugLevel = {
    DBG_ERROR, // SYSTEM
    DBG_ERROR, // WEB
    DBG_ERROR, // NETWORK
    DBG_ERROR, // OSC
    DBG_ERROR, // I2C_BUS
    DBG_ERROR, // FADER_CORE
    DBG_OFF,   // FADER_POSITION
    DBG_ERROR, // TOUCH_CORE
    DBG_OFF,   // TOUCH_RAW
    DBG_ERROR, // CALIBRATION
    DBG_ERROR, // EEPROM
    DBG_ERROR, // LED_EXEC
    DBG_ERROR  // OLED
  }
};

// Executor LED defaults
ExecConfig execConfig = {
  .baseBrightness = EXECUTOR_BASE_BRIGHTNESS,
  .activeBrightness = EXECUTOR_ACTIVE_BRIGHTNESS,
  .useStaticColor = false,
  .staticRed = 255,
  .staticGreen = 255,
  .staticBlue = 255,
  .useMA3OccupiedBrightness = false,
  .reserved = {0}
};

//================================
// NETWORK CONFIGURATION
//================================
// Default static network settings and UDP destination info.
// These values can be updated at runtime or overridden via web UI.
NetworkConfig netConfig = {
  IPAddress(192, 168, 0, 169),     // staticIP
  IPAddress(192, 168, 0, 1),       // gateway
  IPAddress(255, 255, 255, 0),     // subnet
  IPAddress(192, 168, 0, 10),     // sendToIP (OSC target)
  kOSCPort,                        // oscPort (shared OSC send/receive)
  true                             // useDHCP (fallback to static if false)
};

// Network reset check
bool checkForReset = true;
unsigned long resetCheckStartTime = 0;

// Calibration state flag
bool calibrationInProgress = false;

// MA3 LEDBackground brightness for executors (updated via /ledBrightness OSC; not persisted)
uint8_t execMaBrightness = 0;


//================================
// DEBUG SETTINGS
//================================
#ifdef DEBUG
  bool debugMode = true;  // Enables or disables verbose serial output
#else
  bool debugMode = false;  // Enables or disables verbose serial output
#endif

//================================
// TOUCH SENSOR GLOBALS
//================================
// These are used by the touch driver and web UI configuration.
int autoCalibrationMode = 1;      // 0 = AutoTune off, 1 = AutoTune on
#if defined(TOUCH_SENSOR_MTCH2120)
uint8_t touchThreshold = 128;     // Higher = less sensitive (MTCH2120 per-key threshold)
uint8_t releaseThreshold = 1;     // Hysteresis code 0-7
#else
uint8_t touchThreshold = 12;      // Higher = less sensitive (MPR121 default)
uint8_t releaseThreshold = 6;     // Lower = harder to release (MPR121 default)
#endif


// Page Tracking
int currentOSCPage = 1;  // Default to page 1

void loadDefaultDebugLevels(uint8_t* levels, size_t count) {
  if (levels == nullptr || count == 0) {
    return;
  }

  size_t maxCount = count;
  if (maxCount > DEBUG_CHANNEL_COUNT) {
    maxCount = DEBUG_CHANNEL_COUNT;
  }

  for (size_t i = 0; i < maxCount; ++i) {
    levels[i] = kDefaultDebugLevels[i];
  }
}

const char* debugChannelName(DebugChannel channel) {
  switch (channel) {
    case DBG_CH_SYSTEM: return "System";
    case DBG_CH_WEB: return "Web";
    case DBG_CH_NETWORK: return "Network";
    case DBG_CH_OSC: return "OSC";
    case DBG_CH_I2C_BUS: return "I2C Bus";
    case DBG_CH_FADER_CORE: return "Fader Control";
    case DBG_CH_FADER_POSITION: return "Fader Output";
    case DBG_CH_TOUCH_CORE: return "Touch Core";
    case DBG_CH_TOUCH_RAW: return "Touch Raw";
    case DBG_CH_CALIBRATION: return "Calibration";
    case DBG_CH_EEPROM: return "EEPROM";
    case DBG_CH_LED_EXEC: return "Exec LEDs";
    case DBG_CH_OLED: return "OLED";
    default: return "Unknown";
  }
}
