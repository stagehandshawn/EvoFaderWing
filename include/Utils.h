#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <IPAddress.h>

//================================
// DEBUG FUNCTIONS
//================================

// Debug print functions - only output if debug mode is enabled
void debugPrint(const char* message);
void debugPrintf(const char* format, ...);

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
String getParam(String data, const char* key);

#endif // UTILS_H