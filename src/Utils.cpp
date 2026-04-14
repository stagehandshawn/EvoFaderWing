// Utils.cpp

#include "OLED.h"
#include "Utils.h"
#include "Config.h"
#include <stdarg.h>
#include <string.h>

extern OLED display;

//================================
// DEBUG FUNCTIONS
//================================

static bool isValidDebugLevel(uint8_t value) {
  return value == DBG_OFF || value == DBG_ERROR || value == DBG_DEBUG;
}

static const char* debugChannelPrefix(DebugChannel channel) {
  switch (channel) {
    case DBG_CH_SYSTEM: return "[SYSTEM]";
    case DBG_CH_WEB: return "[WEB]";
    case DBG_CH_NETWORK: return "[NETWORK]";
    case DBG_CH_OSC: return "[OSC]";
    case DBG_CH_I2C_BUS: return "[I2C BUS]";
    case DBG_CH_FADER_CORE: return "[FADER]";
    case DBG_CH_FADER_POSITION: return "[FADER POS]";
    case DBG_CH_TOUCH_CORE: return "[TOUCH]";
    case DBG_CH_TOUCH_RAW: return "[TOUCH RAW]";
    case DBG_CH_CALIBRATION: return "[CAL]";
    case DBG_CH_EEPROM: return "[EEPROM]";
    case DBG_CH_LED_EXEC: return "[LED EXEC]";
    case DBG_CH_OLED: return "[OLED]";
    default: return "[LOG]";
  }
}

static bool startsWithExplicitPrefix(const char* text) {
  if (text == nullptr) {
    return false;
  }
  const char* p = text;
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  return *p == '[';
}

bool debugEnabled(DebugChannel channel, DebugLevel needLevel) {
  if (!debugMode || !Fconfig.serialDebug) {
    return false;
  }
  if ((uint8_t)channel >= DBG_CH_COUNT) {
    return false;
  }
  if ((uint8_t)needLevel > (uint8_t)DBG_DEBUG) {
    return false;
  }

  uint8_t configured = Fconfig.debugLevel[(uint8_t)channel];
  if (!isValidDebugLevel(configured)) {
    configured = DBG_ERROR;
  }

  return configured >= (uint8_t)needLevel;
}

static void debugVLog(DebugChannel channel, DebugLevel level, const char* format, va_list args) {
  if (format == nullptr) {
    return;
  }
  if (!debugEnabled(channel, level)) {
    return;
  }

  char buffer[192];
  vsnprintf(buffer, sizeof(buffer), format, args);

  const bool hasExplicitPrefix = startsWithExplicitPrefix(buffer);
  const char* prefix = debugChannelPrefix(channel);
  size_t bufferLen = strlen(buffer);
  bool hasTrailingNewline = (bufferLen > 0 && buffer[bufferLen - 1] == '\n');

  if (hasExplicitPrefix) {
    if (hasTrailingNewline) {
      Serial.print(buffer);
    } else {
      Serial.println(buffer);
    }
    return;
  }

  if (hasTrailingNewline) {
    Serial.print(prefix);
    Serial.print(" ");
    Serial.print(buffer);
  } else {
    Serial.print(prefix);
    Serial.print(" ");
    Serial.println(buffer);
  }
}

void debugLog(DebugChannel channel, DebugLevel level, const char* format, ...) {
  va_list args;
  va_start(args, format);
  debugVLog(channel, level, format, args);
  va_end(args);
}

void debugPrint(const char* message) {
  debugLog(DBG_CH_SYSTEM, DBG_DEBUG, "%s", message ? message : "");
}

void debugPrintf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  debugVLog(DBG_CH_SYSTEM, DBG_DEBUG, format, args);
  va_end(args);
}

//================================
// IP ADDRESS UTILITIES
//================================

String ipToString(IPAddress ip) {
  char buf[16];
  sprintf(buf, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(buf);
}

IPAddress stringToIP(const String &str) {
  int parts[4] = {0};
  sscanf(str.c_str(), "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  return IPAddress(parts[0], parts[1], parts[2], parts[3]);
}

//================================
// WEB PARAMETER PARSING
//================================

String getParam(const String& data, const char* key) {
  int start = data.indexOf(String(key) + "=");
  if (start == -1) return "";
  start += strlen(key) + 1;
  int end = data.indexOf('&', start);
  if (end == -1) end = data.length();
  return data.substring(start, end);
}

//================================
// UPLOAD Function 
//================================
//Upload without pressing button, using python script, takes one second try
void checkSerialForReboot() {
    static String commandBuffer = "";
    
    // Read all available characters and buffer them
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            // End of command received, process it
            commandBuffer.trim(); // Remove any whitespace
            String cmd = commandBuffer;
            commandBuffer = ""; // Clear buffer for next command
            
            // Process the complete command
            if (cmd.length() > 0) {
                processSerialCommand(cmd);
            }
            return;
        } else {
            // Add character to buffer
            commandBuffer += c;
        }
    }
}

// Send identiy so we can update a specific teensy when more than one is plugged in, used with teensy_auto_upload_multi.py
void processSerialCommand(String cmd) {
    if (cmd == "IDENTIFY") {
        Serial.print("[IDENT] ");
        Serial.print(PROJECT_NAME);
        Serial.print(" v");
        Serial.println(SW_VERSION);
        Serial.flush();
        
    } else if (cmd == "REBOOT_BOOTLOADER") {
        Serial.print("[REBOOT] ");
        Serial.print(PROJECT_NAME);
        Serial.print(" v");
        Serial.print(SW_VERSION);
        Serial.println(" entering bootloader...");
        Serial.flush(); // Important: ensure message is sent before reboot
        delay(100);
        
        // This is the correct method for ALL Teensy models
        _reboot_Teensyduino_();
        
    } else if (cmd == "REBOOT_NORMAL") {
        Serial.print("[REBOOT] ");
        Serial.print(PROJECT_NAME);
        Serial.print(" v");
        Serial.print(SW_VERSION);
        Serial.println(" normal reboot requested...");
        Serial.flush();
        delay(100);
        
        // Normal restart using ARM AIRCR register
        resetTeensy();
        
    } else {
        Serial.print("[REBOOT] Unknown command: ");
        Serial.println(cmd);
    }
}

void resetTeensy(){

  SCB_AIRCR = 0x05FA0004;

}
