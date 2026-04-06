// Utils.h
#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <IPAddress.h>
#include "Config.h"

//================================
// DEBUG FUNCTIONS
//================================

// Debug print functions - only output if debug mode is enabled
void debugPrint(const char* message);
void debugPrintf(const char* format, ...);
bool debugEnabled(DebugChannel channel, DebugLevel needLevel);
void debugLog(DebugChannel channel, DebugLevel level, const char* format, ...);


//================================
// UPLOAD FUNCTION
//================================
void checkSerialForReboot();   //Allow us to upload without pressing physical button
void processSerialCommand(String cmd);

//================================
// IP ADDRESS UTILITIES
//================================

// IP address conversion helpers
String ipToString(IPAddress ip);
IPAddress stringToIP(const String &str);

//================================
// WEB PARAMETER PARSING
//================================

// Extract parameter from URL query string
String getParam(const String& data, const char* key);

// Reset Teensy
void resetTeensy();

//================================
// DEBUG MACROS
//================================
#define CH_DEBUG_PRINT(channel, msg) do { debugLog((channel), DBG_DEBUG, "%s", (msg)); } while (0)
#define CH_DEBUG_PRINTF(channel, ...) do { debugLog((channel), DBG_DEBUG, __VA_ARGS__); } while (0)
#define CH_ERROR_PRINT(channel, msg) do { debugLog((channel), DBG_ERROR, "%s", (msg)); } while (0)
#define CH_ERROR_PRINTF(channel, ...) do { debugLog((channel), DBG_ERROR, __VA_ARGS__); } while (0)

#define SYSTEM_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_SYSTEM, (msg))
#define SYSTEM_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_SYSTEM, __VA_ARGS__)
#define SYSTEM_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_SYSTEM, (msg))
#define SYSTEM_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_SYSTEM, __VA_ARGS__)

#define WEB_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_WEB, (msg))
#define WEB_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_WEB, __VA_ARGS__)
#define WEB_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_WEB, (msg))
#define WEB_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_WEB, __VA_ARGS__)

#define NETWORK_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_NETWORK, (msg))
#define NETWORK_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_NETWORK, __VA_ARGS__)
#define NETWORK_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_NETWORK, (msg))
#define NETWORK_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_NETWORK, __VA_ARGS__)

#define OSC_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_OSC, (msg))
#define OSC_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_OSC, __VA_ARGS__)
#define OSC_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_OSC, (msg))
#define OSC_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_OSC, __VA_ARGS__)

#define I2C_BUS_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_I2C_BUS, (msg))
#define I2C_BUS_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_I2C_BUS, __VA_ARGS__)
#define I2C_BUS_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_I2C_BUS, (msg))
#define I2C_BUS_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_I2C_BUS, __VA_ARGS__)

#define FADER_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_FADER_CORE, (msg))
#define FADER_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_FADER_CORE, __VA_ARGS__)
#define FADER_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_FADER_CORE, (msg))
#define FADER_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_FADER_CORE, __VA_ARGS__)

#define FADER_POS_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_FADER_POSITION, (msg))
#define FADER_POS_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_FADER_POSITION, __VA_ARGS__)
#define FADER_POS_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_FADER_POSITION, (msg))
#define FADER_POS_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_FADER_POSITION, __VA_ARGS__)

#define TOUCH_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_TOUCH_CORE, (msg))
#define TOUCH_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_TOUCH_CORE, __VA_ARGS__)
#define TOUCH_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_TOUCH_CORE, (msg))
#define TOUCH_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_TOUCH_CORE, __VA_ARGS__)

#define TOUCH_RAW_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_TOUCH_RAW, (msg))
#define TOUCH_RAW_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_TOUCH_RAW, __VA_ARGS__)
#define TOUCH_RAW_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_TOUCH_RAW, (msg))
#define TOUCH_RAW_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_TOUCH_RAW, __VA_ARGS__)

#define CAL_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_CALIBRATION, (msg))
#define CAL_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_CALIBRATION, __VA_ARGS__)
#define CAL_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_CALIBRATION, (msg))
#define CAL_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_CALIBRATION, __VA_ARGS__)

#define EEPROM_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_EEPROM, (msg))
#define EEPROM_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_EEPROM, __VA_ARGS__)
#define EEPROM_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_EEPROM, (msg))
#define EEPROM_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_EEPROM, __VA_ARGS__)

#define LED_EXEC_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_LED_EXEC, (msg))
#define LED_EXEC_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_LED_EXEC, __VA_ARGS__)
#define LED_EXEC_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_LED_EXEC, (msg))
#define LED_EXEC_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_LED_EXEC, __VA_ARGS__)

#define OLED_DEBUG_PRINT(msg) CH_DEBUG_PRINT(DBG_CH_OLED, (msg))
#define OLED_DEBUG_PRINTF(...) CH_DEBUG_PRINTF(DBG_CH_OLED, __VA_ARGS__)
#define OLED_ERROR_PRINT(msg) CH_ERROR_PRINT(DBG_CH_OLED, (msg))
#define OLED_ERROR_PRINTF(...) CH_ERROR_PRINTF(DBG_CH_OLED, __VA_ARGS__)

#endif // UTILS_H
