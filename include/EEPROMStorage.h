// EEPROMStorage.h
#ifndef EEPROM_STORAGE_H
#define EEPROM_STORAGE_H

#include <Arduino.h>
#include "Config.h"

//================================
// EEPROM MEMORY MAP
//================================

// EEPROM signature constants - Each different data type gets its own signature byte

// Change signature when changing adding/subtracting settings too reset the eeprom data with defualts

#define CALCFG_EEPROM_SIGNATURE 0xA4    // Signature for fader calibration
#define FADERCFG_EEPROM_SIGNATURE 0xBA    // Signature for fader configuration
#define NETCFG_EEPROM_SIGNATURE 0x5C    // Signature for network config
#define TOUCHCFG_EEPROM_SIGNATURE 0xC6     // Signature for touch sensor configuration
#define EXECCFG_EEPROM_SIGNATURE 0xD6     // Signature for executor LED configuration

// EEPROM address map with defined layout to ensure organized storage
#define EEPROM_CAL_START 0              // Start of calibration section (original location)
#define NETCFG_EEPROM_ADDR 100          // Network config (keeping original address)
#define EEPROM_CONFIG_START 200         // Start of fader config section
#define EEPROM_TOUCH_START 400          // Start of touch config
#define EEPROM_EXEC_START 520           // Executor LED config
#define EEPROM_RESERVED_START 640       // Reserved for future expansion

// EEPROM layout for calibration data
#define EEPROM_CAL_SIGNATURE_ADDR EEPROM_CAL_START
#define EEPROM_CAL_DATA_ADDR (EEPROM_CAL_SIGNATURE_ADDR + 1)

// EEPROM layout for fader configuration
#define EEPROM_CONFIG_SIGNATURE_ADDR EEPROM_CONFIG_START
#define EEPROM_CONFIG_DATA_ADDR (EEPROM_CONFIG_SIGNATURE_ADDR + 1)

// EEPROM layout for touch sensor configuration
#define EEPROM_TOUCH_SIGNATURE_ADDR EEPROM_TOUCH_START
#define EEPROM_TOUCH_DATA_ADDR (EEPROM_TOUCH_SIGNATURE_ADDR + 1)

// EEPROM layout for executor LED configuration
#define EEPROM_EXEC_SIGNATURE_ADDR EEPROM_EXEC_START
#define EEPROM_EXEC_DATA_ADDR (EEPROM_EXEC_SIGNATURE_ADDR + 1)

//================================
// FUNCTION DECLARATIONS
//================================

// Calibration functions
void saveCalibration();
void loadCalibration();
void checkCalibration();

// Fader configuration functions
void saveFaderConfig();
void loadConfig();

// Network configuration functions
void saveNetworkConfig();
bool loadNetworkConfig();

// Touch sensor configuration functions
void saveTouchConfig();
void loadTouchConfig();

// Executor LED configuration functions
void saveExecConfig();
bool loadExecConfig();

// Combined configuration functions
void loadAllConfig();
void saveAllConfig();

// Reset functions
void resetToDefaults();
void resetNetworkDefaults();

// Debug functions
void dumpEepromConfig();

#endif // EEPROM_STORAGE_H
