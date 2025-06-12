// Config.cpp
#include "Config.h"

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
//USE 13 for MRP121

// Motor direction control pins — each fader has two pins for H-bridge control.
const uint8_t DIR_PINS1[NUM_FADERS] = {26, 28, 30, 32, 34, 36, 38, 40, 10, 51};
const uint8_t DIR_PINS2[NUM_FADERS] = {27, 29, 31, 33, 35, 37, 39, 41, 11, 52};

// Unique OSC IDs for each fader (used to construct OSC address strings).
const uint16_t OSC_IDS[NUM_FADERS] = {201, 202, 203, 204, 205, 206, 207, 208, 209, 210};

//================================
// FADER BEHAVIOR CONFIGURATION
//================================
// Default PID and motor tuning settings shared across all faders.
FaderConfig Fconfig = {
  .motorDeadzone = MOTOR_DEADZONE,
  .defaultPwm = DEFAULT_PWM,
  .calibratePwm = CALIB_PWM,
  .targetTolerance = TARGET_TOLERANCE,
  .sendTolerance = SEND_TOLERANCE,
  .baseBrightness = 5,
  .touchedBrightness = 40,
  .fadeTime = 1000
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
  IPAddress(192, 168, 0, 100),     // sendToIP (OSC target)
  8000,                            // receivePort (OSC listening)
  9000,                            // sendPort (OSC destination)
  true                             // useDHCP (fallback to static if false)
};



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
// These are used by the MPR121 driver and web UI configuration.
int autoCalibrationMode = 2;     // 0 = Off, 1 = Normal, 2 = Conservative
uint8_t touchThreshold = 12;     // Higher = less sensitive
uint8_t releaseThreshold = 6;    // Lower = harder to release


//Network reset check
bool checkForReset = true;
unsigned long resetCheckStartTime = 0;


// Page Tracking
int currentOSCPage = 1;  // Default to page 1