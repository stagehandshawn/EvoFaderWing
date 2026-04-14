// EEPROMStorage.cpp

#include "EEPROMStorage.h"
#include "TouchSensor.h"
#include "Utils.h"
#include <EEPROM.h>
#include "FaderControl.h"
#include "NetworkOSC.h"
#include "NetworkManager.h"
#include "NeoPixelControl.h"
#include "KeyLedControl.h"
#include <string.h>

namespace {

static void resetDebugConfigToDefaults() {
  Fconfig.debugConfigVersion = DEBUG_CONFIG_VERSION;
  loadDefaultDebugLevels(Fconfig.debugLevel, DEBUG_CHANNEL_COUNT);
}

static uint8_t normalizeStoredDebugLevel(uint8_t value, uint8_t defaultValue) {
  if (value == DBG_OFF || value == DBG_ERROR || value == DBG_DEBUG) {
    return value;
  }
  return defaultValue;
}

static void normalizeDebugConfig() {
  uint8_t defaults[DEBUG_CHANNEL_COUNT] = {};
  loadDefaultDebugLevels(defaults, DEBUG_CHANNEL_COUNT);

  if (Fconfig.debugConfigVersion != DEBUG_CONFIG_VERSION) {
    resetDebugConfigToDefaults();
    return;
  }

  for (uint8_t i = 0; i < DEBUG_CHANNEL_COUNT; ++i) {
    Fconfig.debugLevel[i] = normalizeStoredDebugLevel(Fconfig.debugLevel[i], defaults[i]);
  }
}

}  // namespace

//================================
// CALIBRATION FUNCTIONS
//================================

void saveCalibration() {
  EEPROM.write(EEPROM_CAL_SIGNATURE_ADDR, CALCFG_EEPROM_SIGNATURE);
  int addr = EEPROM_CAL_DATA_ADDR;
  for (int i = 0; i < NUM_FADERS; i++) {
    EEPROM.put(addr, faders[i].minVal); addr += sizeof(int);
    EEPROM.put(addr, faders[i].maxVal); addr += sizeof(int);
  }
  EEPROM_DEBUG_PRINT("Calibration saved.");

}

void loadCalibration() {
  int addr = EEPROM_CAL_DATA_ADDR;
  for (int i = 0; i < NUM_FADERS; i++) {
    EEPROM.get(addr, faders[i].minVal); addr += sizeof(int);
    EEPROM.get(addr, faders[i].maxVal); addr += sizeof(int);
    EEPROM_DEBUG_PRINTF("Loaded Fader %d -> Min: %d Max: %d", i, faders[i].minVal, faders[i].maxVal);
  }
}

void checkCalibration() {
  if (EEPROM.read(EEPROM_CAL_SIGNATURE_ADDR) != CALCFG_EEPROM_SIGNATURE) {
    CAL_DEBUG_PRINT("Running calibration...");
    calibrateFaders();

    saveCalibration();
    saveTouchConfig();          // Save default touch configuration as well
  } else {
    loadCalibration();
    loadTouchConfig();
  }
}

//================================
// FADER CONFIG FUNCTIONS 
//================================

void saveFaderConfig() {
  // Write signature
  EEPROM.write(EEPROM_CONFIG_SIGNATURE_ADDR, FADERCFG_EEPROM_SIGNATURE);
  
  // Write configuration (primitive types only)
  EEPROM.put(EEPROM_CONFIG_DATA_ADDR, Fconfig);
  
  EEPROM_DEBUG_PRINT("Fader configuration saved to EEPROM.");
}

void loadConfig() {
  uint8_t signature = EEPROM.read(EEPROM_CONFIG_SIGNATURE_ADDR);

  if (signature == FADERCFG_EEPROM_SIGNATURE) {
    EEPROM.get(EEPROM_CONFIG_DATA_ADDR, Fconfig);
  } else {
    EEPROM_DEBUG_PRINT("No valid fader configuration in EEPROM, using defaults.");
    resetDebugConfigToDefaults();
  }

  if (signature == FADERCFG_EEPROM_SIGNATURE) {
    Fconfig.serialDebug = Fconfig.serialDebug ? true : false;
    Fconfig.sendKeystrokes = Fconfig.sendKeystrokes ? true : false;
    Fconfig.useLevelPixels = Fconfig.useLevelPixels ? true : false;
    Fconfig.allowFaderOscWithoutTouch = Fconfig.allowFaderOscWithoutTouch ? true : false;
    if (Fconfig.slowZone > 100) Fconfig.slowZone = 100;
    if (Fconfig.fastZone > 100) Fconfig.fastZone = 100;
    if (Fconfig.fastZone <= Fconfig.slowZone) {
      Fconfig.slowZone = SLOW_ZONE;
      Fconfig.fastZone = FAST_ZONE;
    }
    normalizeDebugConfig();
    EEPROM_DEBUG_PRINT("Fader configuration loaded from EEPROM.");
  }

  debugMode = Fconfig.serialDebug;
}

//================================
// NETWORK CONFIG FUNCTIONS
//================================

void saveNetworkConfig() {
  int addr = NETCFG_EEPROM_ADDR;
  EEPROM.write(addr++, NETCFG_EEPROM_SIGNATURE);
 
  // Save each field in the NetworkConfig struct
  // Static IP
  for (int i = 0; i < 4; i++) EEPROM.write(addr++, netConfig.staticIP[i]);
  // Gateway
  for (int i = 0; i < 4; i++) EEPROM.write(addr++, netConfig.gateway[i]);
  // Subnet
  for (int i = 0; i < 4; i++) EEPROM.write(addr++, netConfig.subnet[i]);
  // Send-to IP
  for (int i = 0; i < 4; i++) EEPROM.write(addr++, netConfig.sendToIP[i]);
 
  // Shared OSC port
  EEPROM.put(addr, netConfig.oscPort); addr += sizeof(uint16_t);
 
  // DHCP flag
  EEPROM.write(addr++, netConfig.useDHCP ? 1 : 0);

  networkRequestReconfigure();
  displayIPAddress();
  EEPROM_DEBUG_PRINT("Network configuration saved to EEPROM.");
}
 
bool loadNetworkConfig() {
  int addr = NETCFG_EEPROM_ADDR;
  uint8_t signature = EEPROM.read(addr++);
  if (signature != NETCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("No valid network config in EEPROM, using defaults.");
    return false;
  }
 
  // Load each field from EEPROM
  for (int i = 0; i < 4; i++) netConfig.staticIP[i] = EEPROM.read(addr++);
  for (int i = 0; i < 4; i++) netConfig.gateway[i]  = EEPROM.read(addr++);
  for (int i = 0; i < 4; i++) netConfig.subnet[i]   = EEPROM.read(addr++);
  for (int i = 0; i < 4; i++) netConfig.sendToIP[i] = EEPROM.read(addr++);

  EEPROM.get(addr, netConfig.oscPort); addr += sizeof(uint16_t);
  netConfig.useDHCP = EEPROM.read(addr++) ? true : false;

  if (netConfig.oscPort == 0) {
    netConfig.oscPort = kOSCPort;
  }

  EEPROM_DEBUG_PRINT("Network config loaded from EEPROM.");
  return true;
}

//================================
// TOUCH SENSOR CONFIG FUNCTIONS
//================================

void saveTouchConfig() {
  // Create a temporary configuration structure
  TouchConfig touchConfig;
  
  // Copy current settings to struct
  touchConfig.autoCalibrationMode = autoCalibrationMode;
  touchConfig.touchThreshold = touchThreshold;
  touchConfig.releaseThreshold = releaseThreshold;
  
  // Initialize reserved space to zero
  for (size_t i = 0; i < sizeof(touchConfig.reserved); i++) {
    touchConfig.reserved[i] = 0;
  }
  
  // Write signature
  EEPROM.write(EEPROM_TOUCH_SIGNATURE_ADDR, TOUCHCFG_EEPROM_SIGNATURE);
  
  // Write configuration
  EEPROM.put(EEPROM_TOUCH_DATA_ADDR, touchConfig);
  
  EEPROM_DEBUG_PRINT("Touch sensor configuration saved to EEPROM.");

}

void loadTouchConfig() {
  // Check signature
  if (EEPROM.read(EEPROM_TOUCH_SIGNATURE_ADDR) == TOUCHCFG_EEPROM_SIGNATURE) {
    // Create a temporary structure to hold the data
    TouchConfig touchConfig;
    
    // Load configuration
    EEPROM.get(EEPROM_TOUCH_DATA_ADDR, touchConfig);

    bool normalized = false;

    // Clamp to valid range in case older values were stored or sensor type changed
    autoCalibrationMode = constrain(touchConfig.autoCalibrationMode, 0, 1);
    if (autoCalibrationMode != touchConfig.autoCalibrationMode) {
      normalized = true;
    }

    uint8_t normalizedTouchThreshold = constrain(touchConfig.touchThreshold, 1, 255);
    uint8_t normalizedReleaseThreshold = touchConfig.releaseThreshold;

#if defined(TOUCH_SENSOR_MTCH2120)
    normalizedReleaseThreshold = constrain(normalizedReleaseThreshold, 0, 7);  // HYS code 0-7
#else
    normalizedReleaseThreshold = constrain(normalizedReleaseThreshold, 1, 255);
    if (normalizedTouchThreshold < 2) {
      normalizedTouchThreshold = 2;  // Keep room for release < touch
      normalized = true;
    }
    if (normalizedReleaseThreshold >= normalizedTouchThreshold) {
      normalizedReleaseThreshold = normalizedTouchThreshold - 1;
      normalized = true;
    }
#endif

    if (normalizedTouchThreshold != touchConfig.touchThreshold ||
        normalizedReleaseThreshold != touchConfig.releaseThreshold) {
      normalized = true;
    }

    // Apply loaded values to the global variables
    touchThreshold = normalizedTouchThreshold;
    releaseThreshold = normalizedReleaseThreshold;
    
    EEPROM_DEBUG_PRINT("Touch sensor configuration loaded from EEPROM.");
    if (normalized) {
      TOUCH_DEBUG_PRINT("Touch config normalized for active touch controller.");
    }
    
    // Apply loaded settings to the sensor (no calibration here; run separately after faders are parked)
    setAutoTouchCalibration(autoCalibrationMode);
  } else {
    EEPROM_DEBUG_PRINT("No valid touch configuration in EEPROM, using defaults.");
  }
}

//================================
// EXECUTOR CONFIG FUNCTIONS
//================================

void saveExecConfig() {
  EEPROM.write(EEPROM_EXEC_SIGNATURE_ADDR, EXECCFG_EEPROM_SIGNATURE);
  EEPROM.put(EEPROM_EXEC_DATA_ADDR, execConfig);
  EEPROM_DEBUG_PRINT("Executor LED configuration saved to EEPROM.");
}

bool loadExecConfig() {
  if (EEPROM.read(EEPROM_EXEC_SIGNATURE_ADDR) != EXECCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("No valid executor LED configuration in EEPROM, using defaults.");
    return false;
  }

  EEPROM.get(EEPROM_EXEC_DATA_ADDR, execConfig);

  // Normalize data and clear reserved bytes
  execConfig.baseBrightness = constrain(execConfig.baseBrightness, 0, 255);
  execConfig.activeBrightness = constrain(execConfig.activeBrightness, 0, 255);
  execConfig.useStaticColor = execConfig.useStaticColor ? true : false;
  execConfig.staticRed = constrain(execConfig.staticRed, 0, 255);
  execConfig.staticGreen = constrain(execConfig.staticGreen, 0, 255);
  execConfig.staticBlue = constrain(execConfig.staticBlue, 0, 255);
  execConfig.reserved[0] = 0;
  execConfig.reserved[1] = 0;

  markKeyLedsDirty();
  EEPROM_DEBUG_PRINT("Executor LED configuration loaded from EEPROM.");
  return true;
}

//================================
// COMBINED CONFIGURATION FUNCTIONS
//================================

void loadAllConfig() {
  // Load each configuration type
  loadConfig();          // Load fader configuration
  loadNetworkConfig();   // Load network configuration
  loadTouchConfig();     // Load touch sensor configuration
  loadExecConfig();      // Load executor LED configuration
  loadCalibration();
}

void saveAllConfig() {
  // Save each configuration type
  saveFaderConfig();     // Save fader configuration
  saveNetworkConfig();   // Save network configuration
  saveTouchConfig();     // Save touch sensor configuration
  saveExecConfig();      // Save executor LED configuration
  saveCalibration();
}

//================================
// RESET FUNCTIONS
//================================

void resetToDefaults() {
  // Reset config to defaults using the macro values
  Fconfig.minPwm = MIN_PWM;
  Fconfig.maxPwm = MAX_PWM;
  Fconfig.calibratePwm = CALIB_PWM;
  Fconfig.targetTolerance = TARGET_TOLERANCE;
  Fconfig.sendTolerance = SEND_TOLERANCE;
  Fconfig.slowZone = SLOW_ZONE;
  Fconfig.fastZone = FAST_ZONE;
  Fconfig.baseBrightness = 5;
  Fconfig.touchedBrightness = 40;
  Fconfig.fadeTime = 500;
  Fconfig.serialDebug = false;
  Fconfig.sendKeystrokes = false;
  Fconfig.useLevelPixels = false;
  Fconfig.allowFaderOscWithoutTouch = true;
  resetDebugConfigToDefaults();

  // Reset executor LED settings
  execConfig.baseBrightness = EXECUTOR_BASE_BRIGHTNESS;
  execConfig.activeBrightness = EXECUTOR_ACTIVE_BRIGHTNESS;
  execConfig.useStaticColor = false;
  execConfig.staticRed = 255;
  execConfig.staticGreen = 255;
  execConfig.staticBlue = 255;
  execConfig.reserved[0] = 0;
  execConfig.reserved[1] = 0;

  
  // Reset network settings to defaults
  netConfig.useDHCP = true;
  netConfig.staticIP = IPAddress(192, 168, 0, 169);
  netConfig.gateway = IPAddress(192, 168, 0, 1);
  netConfig.subnet = IPAddress(255, 255, 255, 0);
  netConfig.sendToIP = IPAddress(192, 168, 0, 10);
  netConfig.oscPort = kOSCPort;

  
  // Reset touch settings
  autoCalibrationMode = 1;
#if defined(TOUCH_SENSOR_MTCH2120)
  touchThreshold = 128;
  releaseThreshold = 1;
#else
  touchThreshold = 12;
  releaseThreshold = 6;
#endif
  
  setAutoTouchCalibration(autoCalibrationMode);
  runTouchCalibration();
  
  // Keep runtime debug flag in sync with defaults
  debugMode = Fconfig.serialDebug;
  
  // Save all defaults to EEPROM
  saveAllConfig();

  markKeyLedsDirty();
  SYSTEM_DEBUG_PRINT("All settings reset to defaults");
}

void resetNetworkDefaults() {
  // Reset network config to defaults
  netConfig.useDHCP = true;
  netConfig.staticIP = IPAddress(192, 168, 0, 169);
  netConfig.gateway = IPAddress(192, 168, 0, 1);
  netConfig.subnet = IPAddress(255, 255, 255, 0);
  netConfig.sendToIP = IPAddress(192, 168, 0, 10);
  netConfig.oscPort = kOSCPort;
  
  // Save to EEPROM
  saveNetworkConfig();

  flashAllFadersRed();
  displayShowResetHeader();
  delay(3000);
  
   displayIPAddress();

  NETWORK_DEBUG_PRINT("Network settings reset to defaults");
}

//================================
// DEBUG FUNCTIONS
//================================

void dumpEepromConfig() {
  bool currentDebugMode = debugMode;
  bool currentSerialDebug = Fconfig.serialDebug;
  uint8_t currentLevels[DEBUG_CHANNEL_COUNT] = {};
  memcpy(currentLevels, Fconfig.debugLevel, sizeof(currentLevels));

  debugMode = true;
  Fconfig.serialDebug = true;
  for (uint8_t i = 0; i < DEBUG_CHANNEL_COUNT; ++i) {
    Fconfig.debugLevel[i] = DBG_DEBUG;
  }

  EEPROM_DEBUG_PRINT("\n===== EEPROM CONFIGURATION DUMP =====\n");
  
  // Check calibration data
  EEPROM_DEBUG_PRINT("\n--- Fader Calibration ---");
  if (EEPROM.read(EEPROM_CAL_SIGNATURE_ADDR) == CALCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("Calibration data is valid");
    
    int addr = EEPROM_CAL_DATA_ADDR;
    for (int i = 0; i < NUM_FADERS; i++) {
      int minVal, maxVal;
      EEPROM.get(addr, minVal); addr += sizeof(int);
      EEPROM.get(addr, maxVal); addr += sizeof(int);
      EEPROM_DEBUG_PRINTF("Fader %d: Min=%d, Max=%d, Range=%d", i, minVal, maxVal, maxVal - minVal);
    }
  } else {
    EEPROM_ERROR_PRINTF("Calibration data not found (signature=0x%02X, expected=0x%02X)",
                        EEPROM.read(EEPROM_CAL_SIGNATURE_ADDR), CALCFG_EEPROM_SIGNATURE);
  }
  
  // Check fader configuration
  EEPROM_DEBUG_PRINT("\n--- Fader Configuration ---");
  uint8_t faderSignature = EEPROM.read(EEPROM_CONFIG_SIGNATURE_ADDR);
  if (faderSignature == FADERCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("Fader configuration is valid");

    FaderConfig storedConfig;
    EEPROM.get(EEPROM_CONFIG_DATA_ADDR, storedConfig);

    EEPROM_DEBUG_PRINTF("Min PWM: %d", storedConfig.minPwm);
    EEPROM_DEBUG_PRINTF("Default PWM: %d", storedConfig.maxPwm);
    EEPROM_DEBUG_PRINTF("Calibration PWM: %d", storedConfig.calibratePwm);
    EEPROM_DEBUG_PRINTF("Target Tolerance: %d", storedConfig.targetTolerance);
    EEPROM_DEBUG_PRINTF("Send Tolerance: %d", storedConfig.sendTolerance);
    EEPROM_DEBUG_PRINTF("Slow Zone: %d", storedConfig.slowZone);
    EEPROM_DEBUG_PRINTF("Fast Zone: %d", storedConfig.fastZone);
    EEPROM_DEBUG_PRINTF("Base Brightness: %d", storedConfig.baseBrightness);
    EEPROM_DEBUG_PRINTF("Touched Brightness: %d", storedConfig.touchedBrightness);
    EEPROM_DEBUG_PRINTF("Fade Time (ms): %lu", storedConfig.fadeTime);
    EEPROM_DEBUG_PRINTF("Serial Debug: %s", storedConfig.serialDebug ? "Enabled" : "Disabled");
    EEPROM_DEBUG_PRINTF("Send Keystrokes: %s", storedConfig.sendKeystrokes ? "Enabled" : "Disabled");
    EEPROM_DEBUG_PRINTF("Allow OSC Without Touch: %s", storedConfig.allowFaderOscWithoutTouch ? "Enabled" : "Disabled");
    EEPROM_DEBUG_PRINTF("Debug Config Version: %u", storedConfig.debugConfigVersion);
    for (uint8_t i = 0; i < DEBUG_CHANNEL_COUNT; ++i) {
      EEPROM_DEBUG_PRINTF("Debug Channel %-14s: %u", debugChannelName(static_cast<DebugChannel>(i)), storedConfig.debugLevel[i]);
    }
  } else {
    EEPROM_ERROR_PRINTF("Fader config not found (signature=0x%02X, expected=0x%02X)",
                        EEPROM.read(EEPROM_CONFIG_SIGNATURE_ADDR), FADERCFG_EEPROM_SIGNATURE);
  }
  
  // Check network configuration
  EEPROM_DEBUG_PRINT("\n--- Network Configuration ---");
  uint8_t networkSignature = EEPROM.read(NETCFG_EEPROM_ADDR);
  if (networkSignature == NETCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("Network configuration is valid");
    
    // Read network config manually from EEPROM
    int addr = NETCFG_EEPROM_ADDR + 1; // Skip signature
    
    IPAddress staticIP, gateway, subnet, sendToIP;
    for (int i = 0; i < 4; i++) staticIP[i] = EEPROM.read(addr++);
    for (int i = 0; i < 4; i++) gateway[i] = EEPROM.read(addr++);
    for (int i = 0; i < 4; i++) subnet[i] = EEPROM.read(addr++);
    for (int i = 0; i < 4; i++) sendToIP[i] = EEPROM.read(addr++);
    
    uint16_t oscPort = kOSCPort;
    EEPROM.get(addr, oscPort); addr += sizeof(uint16_t);
    bool useDHCP = EEPROM.read(addr) ? true : false;
    
    EEPROM_DEBUG_PRINTF("Use DHCP: %s", useDHCP ? "Yes" : "No");
    EEPROM_DEBUG_PRINTF("Static IP: %d.%d.%d.%d", staticIP[0], staticIP[1], staticIP[2], staticIP[3]);
    EEPROM_DEBUG_PRINTF("Gateway: %d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
    EEPROM_DEBUG_PRINTF("Subnet: %d.%d.%d.%d", subnet[0], subnet[1], subnet[2], subnet[3]);
    EEPROM_DEBUG_PRINTF("Send-To IP: %d.%d.%d.%d", sendToIP[0], sendToIP[1], sendToIP[2], sendToIP[3]);
    EEPROM_DEBUG_PRINTF("OSC Port: %d", oscPort);
  } else {
    EEPROM_ERROR_PRINTF("Network config not found (signature=0x%02X, expected=0x%02X)",
                        EEPROM.read(NETCFG_EEPROM_ADDR), NETCFG_EEPROM_SIGNATURE);
  }
  
  // Check touch configuration
  EEPROM_DEBUG_PRINT("\n--- Touch Sensor Configuration ---");
  if (EEPROM.read(EEPROM_TOUCH_SIGNATURE_ADDR) == TOUCHCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("Touch sensor configuration is valid");
    
    TouchConfig touchConfig;
    EEPROM.get(EEPROM_TOUCH_DATA_ADDR, touchConfig);
    
    EEPROM_DEBUG_PRINTF("Auto Calibration Mode: %d", touchConfig.autoCalibrationMode);
    EEPROM_DEBUG_PRINTF("Touch Threshold: %d", touchConfig.touchThreshold);
    EEPROM_DEBUG_PRINTF("Hysteresis Code: %d", touchConfig.releaseThreshold);
  } else {
    EEPROM_ERROR_PRINTF("Touch config not found (signature=0x%02X, expected=0x%02X)",
                        EEPROM.read(EEPROM_TOUCH_SIGNATURE_ADDR), TOUCHCFG_EEPROM_SIGNATURE);
  }

  // Check executor LED configuration
  EEPROM_DEBUG_PRINT("\n--- Executor LED Configuration ---");
  if (EEPROM.read(EEPROM_EXEC_SIGNATURE_ADDR) == EXECCFG_EEPROM_SIGNATURE) {
    EEPROM_DEBUG_PRINT("Executor configuration is valid");

    ExecConfig storedExec;
    EEPROM.get(EEPROM_EXEC_DATA_ADDR, storedExec);

    EEPROM_DEBUG_PRINTF("Base Brightness: %d", storedExec.baseBrightness);
    EEPROM_DEBUG_PRINTF("Active Brightness: %d", storedExec.activeBrightness);
    EEPROM_DEBUG_PRINTF("Use Static Color: %s", storedExec.useStaticColor ? "Yes" : "No");
    EEPROM_DEBUG_PRINTF("Static Color: R%d G%d B%d", storedExec.staticRed, storedExec.staticGreen, storedExec.staticBlue);
  } else {
    EEPROM_ERROR_PRINTF("Executor config not found (signature=0x%02X, expected=0x%02X)",
                        EEPROM.read(EEPROM_EXEC_SIGNATURE_ADDR), EXECCFG_EEPROM_SIGNATURE);
  }

  
  EEPROM_DEBUG_PRINT("\n===== END OF EEPROM DUMP =====\n");

  debugMode = currentDebugMode;
  Fconfig.serialDebug = currentSerialDebug;
  memcpy(Fconfig.debugLevel, currentLevels, sizeof(currentLevels));

  displayIPAddress();

}
