#include "OLED.h"
extern OLED display;
#include "Utils.h"
#include "Config.h"
#include <stdarg.h>

//================================
// DEBUG FUNCTIONS
//================================

void debugPrint(const char* message) {
  if (debugMode) {
    Serial.println(message);
    display.addDebugLine(message);
  }
}

void debugPrintf(const char* format, ...) {
  if (debugMode) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Check if the format string already ends with a newline
    size_t len = strlen(format);
    if (len > 0 && format[len-1] == '\n') {
      Serial.print(buffer); // Already has newline
      display.addDebugLine(buffer);
    } else {
      Serial.println(buffer); // Add newline
      display.addDebugLine(buffer);
    }
  }
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

String getParam(String data, const char* key) {
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
//Upload without pressing button, using python script, it does not fully upload cant get it working but it puts into bootloader so we can manually press upload
void checkSerialForReboot() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim(); // Remove any whitespace/newlines
        
        if (cmd == "REBOOT_BOOTLOADER") {
            Serial.println("[REBOOT] Command received. Entering bootloader...");
            Serial.flush(); // Important: ensure message is sent before reboot
            delay(100);
            
            // This is the correct method for ALL Teensy models
            _reboot_Teensyduino_();
            
        } else if (cmd == "REBOOT_NORMAL") {
            Serial.println("[REBOOT] Normal reboot requested...");
            Serial.flush();
            delay(100);
            
            // Normal restart using ARM AIRCR register
            SCB_AIRCR = 0x05FA0004;
            
        } else {
            Serial.print("[REBOOT] Unknown command: ");
            Serial.println(cmd);
        }
    }
}