// EEPROMStorage.cpp

#include "EEPROMStorage.h"
#include "TouchSensor.h"
#include "Utils.h"
#include <EEPROM.h>
#include "FaderControl.h"
#include "NetworkOSC.h"

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
  debugPrint("Calibration saved.");
}

void loadCalibration() {
  int addr = EEPROM_CAL_DATA_ADDR;
  for (int i = 0; i < NUM_FADERS; i++) {
    EEPROM.get(addr, faders[i].minVal); addr += sizeof(int);
    EEPROM.get(addr, faders[i].maxVal); addr += sizeof(int);
    debugPrintf("Loaded Fader %d → Min: %d Max: %d\n", i, faders[i].minVal, faders[i].maxVal);
  }
}

void checkCalibration() {
  if (EEPROM.read(EEPROM_CAL_SIGNATURE_ADDR) != CALCFG_EEPROM_SIGNATURE) {
    debugPrint("Running calibration...");
    calibrateFaders();
    //manualTouchCalibration();   // Also recalibrate touch sensor

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
  
  debugPrint("Fader configuration saved to EEPROM.");
}

void loadConfig() {
  // Check signature
  if (EEPROM.read(EEPROM_CONFIG_SIGNATURE_ADDR) == FADERCFG_EEPROM_SIGNATURE) {
    // Load configuration
    EEPROM.get(EEPROM_CONFIG_DATA_ADDR, Fconfig);
    debugPrint("Fader configuration loaded from EEPROM.");
  } else {
    debugPrint("No valid fader configuration in EEPROM, using defaults.");
    // Default config values already set in the struct initialization
  }
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
 
  // Ports
  EEPROM.put(addr, netConfig.receivePort); addr += sizeof(uint16_t);
  EEPROM.put(addr, netConfig.sendPort);    addr += sizeof(uint16_t);
 
  // DHCP flag
  EEPROM.write(addr++, netConfig.useDHCP ? 1 : 0);
 
  bool configChanged = false;
  int checkAddr = NETCFG_EEPROM_ADDR + 1; // Skip signature
  NetworkConfig oldConfig;

  // Load old staticIP
  for (int i = 0; i < 4; i++) oldConfig.staticIP[i] = EEPROM.read(checkAddr++);
  // Load old gateway
  for (int i = 0; i < 4; i++) oldConfig.gateway[i] = EEPROM.read(checkAddr++);
  // Load old subnet
  for (int i = 0; i < 4; i++) oldConfig.subnet[i] = EEPROM.read(checkAddr++);
  // Load old sendToIP
  for (int i = 0; i < 4; i++) oldConfig.sendToIP[i] = EEPROM.read(checkAddr++);
  // Load old ports
  EEPROM.get(checkAddr, oldConfig.receivePort); checkAddr += sizeof(uint16_t);
  EEPROM.get(checkAddr, oldConfig.sendPort);    checkAddr += sizeof(uint16_t);
  // Load old DHCP flag
  oldConfig.useDHCP = EEPROM.read(checkAddr++) ? true : false;

  // Check if any relevant values have changed
  for (int i = 0; i < 4; i++) {
    if (oldConfig.staticIP[i] != netConfig.staticIP[i]) configChanged = true;
    if (oldConfig.gateway[i] != netConfig.gateway[i]) configChanged = true;
    if (oldConfig.subnet[i] != netConfig.subnet[i]) configChanged = true;
  }
  if (oldConfig.useDHCP != netConfig.useDHCP) configChanged = true;
  //if (oldConfig.receivePort != netConfig.receivePort || oldConfig.sendPort != netConfig.sendPort) configChanged = true;

  if (configChanged) {
    Ethernet.end();
    setupNetwork();  // includes udp.begin(newReceivePort)
  }

  displayIPAddress();
}
 
bool loadNetworkConfig() {
  int addr = NETCFG_EEPROM_ADDR;
  if (EEPROM.read(addr++) != NETCFG_EEPROM_SIGNATURE) {
    debugPrint("No valid network config in EEPROM, using defaults.");
    return false;
  }
 
  // Load each field from EEPROM
  for (int i = 0; i < 4; i++) netConfig.staticIP[i] = EEPROM.read(addr++);
  for (int i = 0; i < 4; i++) netConfig.gateway[i]  = EEPROM.read(addr++);
  for (int i = 0; i < 4; i++) netConfig.subnet[i]   = EEPROM.read(addr++);
  for (int i = 0; i < 4; i++) netConfig.sendToIP[i] = EEPROM.read(addr++);
 
  EEPROM.get(addr, netConfig.receivePort); addr += sizeof(uint16_t);
  EEPROM.get(addr, netConfig.sendPort);    addr += sizeof(uint16_t);
 
  netConfig.useDHCP = EEPROM.read(addr++) ? true : false;
 

  debugPrint("Network config loaded from EEPROM.");
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
  
  debugPrint("Touch sensor configuration saved to EEPROM.");
}

void loadTouchConfig() {
  // Check signature
  if (EEPROM.read(EEPROM_TOUCH_SIGNATURE_ADDR) == TOUCHCFG_EEPROM_SIGNATURE) {
    // Create a temporary structure to hold the data
    TouchConfig touchConfig;
    
    // Load configuration
    EEPROM.get(EEPROM_TOUCH_DATA_ADDR, touchConfig);
    
    // Apply loaded values to the global variables
    autoCalibrationMode = touchConfig.autoCalibrationMode;
    touchThreshold = touchConfig.touchThreshold;
    releaseThreshold = touchConfig.releaseThreshold;
    
    debugPrint("Touch sensor configuration loaded from EEPROM.");
    
    // Apply loaded settings to the sensor
    setAutoTouchCalibration(autoCalibrationMode);
  } else {
    debugPrint("No valid touch configuration in EEPROM, using defaults.");
  }
}

//================================
// COMBINED CONFIGURATION FUNCTIONS
//================================

void loadAllConfig() {
  // Load each configuration type
  loadConfig();          // Load fader configuration
  loadNetworkConfig();   // Load network configuration
  loadTouchConfig();     // Load touch sensor configuration
  loadCalibration();
}

void saveAllConfig() {
  // Save each configuration type
  saveFaderConfig();     // Save fader configuration
  saveNetworkConfig();   // Save network configuration
  saveTouchConfig();     // Save touch sensor configuration
  saveCalibration();
}

//================================
// RESET FUNCTIONS
//================================

void resetToDefaults() {
  // Reset config to defaults using the macro values
  Fconfig.motorDeadzone = MOTOR_DEADZONE;
  Fconfig.defaultPwm = DEFAULT_PWM;
  Fconfig.calibratePwm = CALIB_PWM;
  Fconfig.targetTolerance = TARGET_TOLERANCE;
  Fconfig.sendTolerance = SEND_TOLERANCE;
  
  
  // Reset touch settings
  autoCalibrationMode = 2; // Default value (conservative)
  touchThreshold = 12;     // Default value
  releaseThreshold = 6;    // Default value
  
  setAutoTouchCalibration(autoCalibrationMode);
  manualTouchCalibration();
  
  // Save all defaults to EEPROM
  saveAllConfig();
  
  debugPrint("All settings reset to defaults");
}

void resetNetworkDefaults() {
  // Reset network config to defaults
  netConfig.useDHCP = true;
  netConfig.staticIP = IPAddress(192, 168, 0, 169);
  netConfig.gateway = IPAddress(192, 168, 0, 1);
  netConfig.subnet = IPAddress(255, 255, 255, 0);
  netConfig.sendToIP = IPAddress(192, 168, 0, 100);
  netConfig.receivePort = 8000;
  netConfig.sendPort = 9000;
  
  // Save to EEPROM
  saveNetworkConfig();
  
   displayIPAddress();

  debugPrint("Network settings reset to defaults");
}

//================================
// DEBUG FUNCTIONS
//================================

void dumpEepromConfig() {
  debugPrint("\n===== EEPROM CONFIGURATION DUMP =====\n");
  
  // Check calibration data
  debugPrint("\n--- Fader Calibration ---");
  if (EEPROM.read(EEPROM_CAL_SIGNATURE_ADDR) == CALCFG_EEPROM_SIGNATURE) {
    debugPrint("Calibration data is valid");
    
    int addr = EEPROM_CAL_DATA_ADDR;
    for (int i = 0; i < NUM_FADERS; i++) {
      int minVal, maxVal;
      EEPROM.get(addr, minVal); addr += sizeof(int);
      EEPROM.get(addr, maxVal); addr += sizeof(int);
      debugPrintf("Fader %d: Min=%d, Max=%d, Range=%d\n", 
                 i, minVal, maxVal, maxVal - minVal);
    }
  } else {
    debugPrintf("Calibration data not found (signature=0x%02X, expected=0x%02X)\n", 
               EEPROM.read(EEPROM_CAL_SIGNATURE_ADDR), CALCFG_EEPROM_SIGNATURE);
  }
  
  // Check fader configuration
  debugPrint("\n--- Fader Configuration ---");
  if (EEPROM.read(EEPROM_CONFIG_SIGNATURE_ADDR) == FADERCFG_EEPROM_SIGNATURE) {
    debugPrint("Fader configuration is valid");
    
    FaderConfig storedConfig;
    EEPROM.get(EEPROM_CONFIG_DATA_ADDR, storedConfig);
    
    debugPrintf("Motor Deadzone: %d\n", storedConfig.motorDeadzone);
    debugPrintf("Default PWM: %d\n", storedConfig.defaultPwm);
    debugPrintf("Calibration PWM: %d\n", storedConfig.calibratePwm);
    debugPrintf("Target Tolerance: %d\n", storedConfig.targetTolerance);
    debugPrintf("Send Tolerance: %d\n", storedConfig.sendTolerance);
  } else {
    debugPrintf("Fader config not found (signature=0x%02X, expected=0x%02X)\n", 
               EEPROM.read(EEPROM_CONFIG_SIGNATURE_ADDR), FADERCFG_EEPROM_SIGNATURE);
  }
  
  // Check network configuration
  debugPrint("\n--- Network Configuration ---");
  if (EEPROM.read(NETCFG_EEPROM_ADDR) == NETCFG_EEPROM_SIGNATURE) {
    debugPrint("Network configuration is valid");
    
    // Read network config manually from EEPROM
    int addr = NETCFG_EEPROM_ADDR + 1; // Skip signature
    
    IPAddress staticIP, gateway, subnet, sendToIP;
    for (int i = 0; i < 4; i++) staticIP[i] = EEPROM.read(addr++);
    for (int i = 0; i < 4; i++) gateway[i] = EEPROM.read(addr++);
    for (int i = 0; i < 4; i++) subnet[i] = EEPROM.read(addr++);
    for (int i = 0; i < 4; i++) sendToIP[i] = EEPROM.read(addr++);
    
    uint16_t receivePort, sendPort;
    EEPROM.get(addr, receivePort); addr += sizeof(uint16_t);
    EEPROM.get(addr, sendPort); addr += sizeof(uint16_t);
    
    bool useDHCP = EEPROM.read(addr) ? true : false;
    
    debugPrintf("Use DHCP: %s\n", useDHCP ? "Yes" : "No");
    debugPrintf("Static IP: %d.%d.%d.%d\n", staticIP[0], staticIP[1], staticIP[2], staticIP[3]);
    debugPrintf("Gateway: %d.%d.%d.%d\n", gateway[0], gateway[1], gateway[2], gateway[3]);
    debugPrintf("Subnet: %d.%d.%d.%d\n", subnet[0], subnet[1], subnet[2], subnet[3]);
    debugPrintf("Send-To IP: %d.%d.%d.%d\n", sendToIP[0], sendToIP[1], sendToIP[2], sendToIP[3]);
    debugPrintf("Receive Port: %d\n", receivePort);
    debugPrintf("Send Port: %d\n", sendPort);
  } else {
    debugPrintf("Network config not found (signature=0x%02X, expected=0x%02X)\n",
               EEPROM.read(NETCFG_EEPROM_ADDR), NETCFG_EEPROM_SIGNATURE);
  }
  
  // Check touch configuration
  debugPrint("\n--- Touch Sensor Configuration ---");
  if (EEPROM.read(EEPROM_TOUCH_SIGNATURE_ADDR) == TOUCHCFG_EEPROM_SIGNATURE) {
    debugPrint("Touch sensor configuration is valid");
    
    TouchConfig touchConfig;
    EEPROM.get(EEPROM_TOUCH_DATA_ADDR, touchConfig);
    
    debugPrintf("Auto Calibration Mode: %d\n", touchConfig.autoCalibrationMode);
    debugPrintf("Touch Threshold: %d\n", touchConfig.touchThreshold);
    debugPrintf("Release Threshold: %d\n", touchConfig.releaseThreshold);
  } else {
    debugPrintf("Touch config not found (signature=0x%02X, expected=0x%02X)\n",
               EEPROM.read(EEPROM_TOUCH_SIGNATURE_ADDR), TOUCHCFG_EEPROM_SIGNATURE);
  }
  

  
  debugPrint("\n===== END OF EEPROM DUMP =====\n");
}