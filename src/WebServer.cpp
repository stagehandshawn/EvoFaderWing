#include "WebServer.h"
#include "Utils.h"
#include "EEPROMStorage.h"
#include "FaderControl.h"
#include "TouchSensor.h"
#include <QNEthernet.h>
#include "NeoPixelControl.h"
#include "OLED.h"
#include "NetworkOSC.h"
#include "NetworkManager.h"
#include "KeyLedControl.h"
#include "Keysend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

using namespace qindesign::network;


//================================
// GLOBAL NETWORK OBJECTS
//================================
EthernetServer server(80);
EthernetClient client;
static bool webServerStarted = false;

static bool writeAllToClient(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return true;
  }

  size_t sent = 0;
  unsigned long lastProgressMs = millis();
  constexpr unsigned long kWriteTimeoutMs = 2000;

  while (sent < len && client.connected()) {
    int available = client.availableForWrite();
    if (available <= 0) {
      Ethernet.loop();
      delay(1);
      if (millis() - lastProgressMs > kWriteTimeoutMs) {
        return false;
      }
      continue;
    }

    size_t chunk = len - sent;
    if (static_cast<size_t>(available) < chunk) {
      chunk = static_cast<size_t>(available);
    }

    size_t written = client.write(data + sent, chunk);
    if (written == 0) {
      Ethernet.loop();
      delay(1);
      if (millis() - lastProgressMs > kWriteTimeoutMs) {
        return false;
      }
      continue;
    }

    sent += written;
    lastProgressMs = millis();
  }

  return sent == len;
}

static void safePrint(const char* s) {
  if (s == nullptr) {
    return;
  }
  writeAllToClient(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

static void safePrint(const String& s) {
  writeAllToClient(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

static void safePrint(char c) {
  writeAllToClient(reinterpret_cast<const uint8_t*>(&c), 1);
}

static void safePrint(const __FlashStringHelper* flashString) {
  if (flashString == nullptr) {
    return;
  }

  PGM_P p = reinterpret_cast<PGM_P>(flashString);
  char chunk[96];
  while (true) {
    size_t count = 0;
    char c = 0;
    while (count < sizeof(chunk)) {
      c = pgm_read_byte(p++);
      if (c == '\0') {
        break;
      }
      chunk[count++] = c;
    }

    if (count > 0) {
      writeAllToClient(reinterpret_cast<const uint8_t*>(chunk), count);
    }

    if (c == '\0') {
      break;
    }
  }
}

template <typename T>
static typename std::enable_if<std::is_arithmetic<T>::value, void>::type safePrint(T value) {
  safePrint(String(value));
}

static void safePrintLn() {
  safePrint("\r\n");
}

template <typename T>
static void safePrintLn(const T& value) {
  safePrint(value);
  safePrint("\r\n");
}

//================================
// SERVER MANAGEMENT
//================================

void startWebServer() {
  server.begin();
  if (!webServerStarted) {
    WEB_DEBUG_PRINT("Web server started at http://");
    WEB_DEBUG_PRINT(ipToString(Ethernet.localIP()).c_str());
    webServerStarted = true;
  }
}

void pollWebServer() {
  client = server.available();
  if (client) {
    handleWebServer();
  }
}

//================================
// VALIDATION FUNCTIONS
//================================

bool isValidIP(IPAddress ip) {
  // Allow any IP except completely invalid ones
  // Note: 0.0.0.0 might be valid in some contexts, but usually indicates an error
  return (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0);
}

bool isValidPort(int port) {
  return (port >= 1 && port <= 65535);
}

int constrainParam(int value, int minVal, int maxVal, int defaultVal) {
  if (value < minVal || value > maxVal) {
    WEB_ERROR_PRINTF("Warning: Value %d out of range [%d-%d], using default %d",
                     value, minVal, maxVal, defaultVal);
    return defaultVal;
  }
  return value;
}

bool parseHexColor(const String& hex, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (hex.length() != 7 || hex.charAt(0) != '#') {
    return false;
  }

  char* endPtr = nullptr;
  unsigned long value = strtoul(hex.c_str() + 1, &endPtr, 16);
  if (endPtr == nullptr || *endPtr != '\0' || value > 0xFFFFFF) {
    return false;
  }

  r = (value >> 16) & 0xFF;
  g = (value >> 8) & 0xFF;
  b = value & 0xFF;
  return true;
}


void sendErrorResponse(const char* errorMsg) {
  safePrintLn("HTTP/1.1 400 Bad Request");
  safePrintLn("Content-Type: text/html");
  safePrintLn("Connection: close");
  safePrintLn();
  safePrintLn("<html><head>");
  safePrintLn("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  safePrintLn("<link rel='icon' type='image/svg+xml' href='/favicon.svg'>");
  safePrintLn("<style>");
  safePrintLn("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #202325; color: #e8e6e3; }");
  safePrintLn(".error-container { background: #181a1b; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.3); max-width: 500px; margin: 50px auto; border: 1px solid #3a3e41; }");
  safePrintLn("h1 { color: #f44336; margin-top: 0; }");
  safePrintLn("p { color: #a8a095; line-height: 1.6; }");
  safePrintLn("a { color: #3391ff; text-decoration: none; font-weight: 500; }");
  safePrintLn("a:hover { text-decoration: underline; }");
  safePrintLn("</style></head><body>");
  safePrintLn("<div class='error-container'>");
  safePrintLn("<h1>Error</h1>");
  safePrint("<p>");
  safePrint(errorMsg);
  safePrintLn("</p>");
  safePrintLn("<p><a href='/'>Return to settings</a></p>");
  safePrintLn("</div></body></html>");
}

//================================
// MAIN REQUEST HANDLER
//================================

void handleWebServer() {
  if (client) {
    WEB_DEBUG_PRINT("New client connected");
    
    // First, read the request line and headers
    String request = "";
    String requestBody = "";
    unsigned long timeout = millis() + 1000; // 1 second timeout
    bool headersDone = false;
    int contentLength = 0;
    
    // Read headers
    while (client.connected() && millis() < timeout) {
      if (client.available()) {
        char c = client.read();
        request += c;
        
        // Check for Content-Length header
        if (!headersDone && request.indexOf("Content-Length: ") > 0 && request.indexOf("\r\n", request.indexOf("Content-Length: ")) > 0) {
          int start = request.indexOf("Content-Length: ") + 16;
          int end = request.indexOf("\r\n", start);
          String contentLengthStr = request.substring(start, end);
          contentLength = contentLengthStr.toInt();

          if (request.startsWith("POST") && contentLength > 0) {
            WEB_DEBUG_PRINTF("Content-Length: %d", contentLength);
          }
        }
        
        // Check for end of headers
        if (!headersDone && request.endsWith("\r\n\r\n")) {
          headersDone = true;
          WEB_DEBUG_PRINT("Headers complete, reading body...");
          
          // If it's a POST with content, we need to read the body
          if (request.startsWith("POST") && contentLength > 0) {
            // Read the rest of the body
            int bytesRead = 0;
            timeout = millis() + 1000; // Reset timeout for body
            
            while (bytesRead < contentLength && client.connected() && millis() < timeout) {
              if (client.available()) {
                char c = client.read();
                requestBody += c;
                bytesRead++;
              }
            }
            
            WEB_DEBUG_PRINTF("Request body received (%d bytes)", requestBody.length());
          }
          
          // Once headers and body are read, we can break the loop
          break;
        }
      }
    }
    
    // Now handle the request with both headers and body
    // Extract request method and path
    int methodEndPos = request.indexOf(' ');
    int pathEndPos = request.indexOf(' ', methodEndPos + 1);
    
    if (methodEndPos > 0 && pathEndPos > methodEndPos) {
      String method = request.substring(0, methodEndPos);          // GET, POST, etc.
      String path = request.substring(methodEndPos + 1, pathEndPos); // /save, /debug, etc.
      
      WEB_DEBUG_PRINTF("Request: %s %s", method.c_str(), path.c_str());
      
      // Determine which type of request to handle
      char requestType = '\0';
      
      // UPDATED ROUTING LOGIC
      if (path.startsWith("/save")) {
        WEB_DEBUG_PRINT("Processing /save request");

        String saveScope = getParam(request, "saveScope");
        if (saveScope == "network_settings") {
          requestType = 'N'; // Network settings only
        } else if (saveScope == "osc_settings") {
          requestType = 'O'; // OSC settings only  
        } else if (saveScope == "calibration_fader" || request.indexOf("calib_pwm=") >= 0) {
          requestType = 'C'; // Calibration settings
        } else if (saveScope == "calibration_touch" || request.indexOf("touchThreshold=") >= 0) {
          requestType = 'T'; // Touch settings
        } else if (saveScope == "led_fader" || saveScope == "led_exec" ||
                   request.indexOf("bb=") >= 0 || request.indexOf("tb=") >= 0 ||
                   request.indexOf("ft=") >= 0 || request.indexOf("lp=") >= 0 ||
                   request.indexOf("eb=") >= 0 || request.indexOf("ea=") >= 0 ||
                   request.indexOf("sc=") >= 0 || request.indexOf("sch=") >= 0) {
          requestType = 'V'; // LED settings only
        } else if (saveScope == "fader_speed" || saveScope == "fader_tolerance" ||
                   saveScope == "fader_touchless" ||
                   request.indexOf("minPwm=") >= 0 || request.indexOf("allowOscWithoutTouch_present=1") >= 0) {
          requestType = 'F'; // Fader settings
        } else if (saveScope == "debug_settings") {
          requestType = 'J'; // Debug settings
        } else {
          // Legacy fallback when saveScope is not present
          bool hasNetworkFields = (request.indexOf("&ip=") >= 0 || request.indexOf("?ip=") >= 0 ||
                                   request.indexOf("dhcp=") >= 0 ||
                                   request.indexOf("gw=") >= 0 || request.indexOf("sn=") >= 0);
          bool hasOSCFields = (request.indexOf("osc_sendip=") >= 0 || request.indexOf("osc_port=") >= 0);

          if (hasNetworkFields && !hasOSCFields) {
            requestType = 'N';
          } else if (hasOSCFields && !hasNetworkFields) {
            requestType = 'O';
          } else if (hasOSCFields && hasNetworkFields) {
            WEB_ERROR_PRINT("Both network and OSC fields detected - treating as OSC");
            requestType = 'O';
          } else if (request.indexOf("calib_pwm=") >= 0) {
            requestType = 'C';
          } else if (request.indexOf("touchThreshold=") >= 0) {
            requestType = 'T';
          } else if (request.indexOf("bb=") >= 0 || request.indexOf("tb=") >= 0 ||
                     request.indexOf("ft=") >= 0 || request.indexOf("lp=") >= 0 ||
                     request.indexOf("eb=") >= 0 || request.indexOf("ea=") >= 0 ||
                     request.indexOf("sc=") >= 0 || request.indexOf("sch=") >= 0) {
            requestType = 'V';
          } else if (request.indexOf("minPwm=") >= 0 || request.indexOf("allowOscWithoutTouch_present=1") >= 0) {
            requestType = 'F';
          }
        }

        if (requestType == '\0') {
          if (saveScope.length() == 0) {
            WEB_ERROR_PRINT("Could not determine request type for /save");
          } else {
            WEB_ERROR_PRINTF("Unknown saveScope '%s'", saveScope.c_str());
          }
          WEB_ERROR_PRINTF("Request: %s", request.c_str());
        } else {
          WEB_DEBUG_PRINTF("[SAVE][SCOPE] %s -> %c", saveScope.c_str(), requestType);
        }
      } else if (path == "/calibrate" && method == "POST") {
        requestType = 'R'; // Run calibration
      } else if (path == "/enable_disabled_faders" && method == "POST") {
        requestType = 'M'; // Re-enable disabled faders
      } else if (path == "/debug" && method == "POST") {
        requestType = 'D'; // Debug mode toggle
      } else if (path == "/dump" && method == "POST") {
        requestType = 'E'; // EEPROM dump
      } else if (path == "/reset_defaults" && method == "POST") {
        requestType = 'X'; // Reset to defaults
      } else if (path == "/reboot" && method == "POST") {
        requestType = 'B'; // Reboot
      } else if (path == "/reset_network" && method == "POST") {
        requestType = 'Z'; // Reset network settings
      } else if (path == "/stats_data") {
        requestType = 'Y'; // Stats JSON data
      } else if (path == "/stats") {  
        requestType = 'S'; // Stats page
      } else if (path == "/fader_settings") {
        requestType = 'G'; // Fader settings page
      } else if (path == "/led_settings") {
        requestType = 'L'; // LED settings page
      } else if (path == "/osc_settings") {
        requestType = 'A'; // OSC settings page
      } else if (path == "/debug_settings") {
        requestType = 'J'; // Debug settings page
      } else if (path.startsWith("/downloadshortcuts")) {
        requestType = 'W'; // XML download
      } else if (path == "/favicon.svg") {
        requestType = 'I'; // Favicon
      } else if (path == "/") {
        requestType = 'H'; // Home/Root page
      }
      
      // Handle the request based on its type
      switch (requestType) {
        case 'N': // Network settings
          handleNetworkSettings(request);
          break;
          
        case 'O': // OSC settings only
          handleOSCSettings(request);
          break;
          
        case 'C': // Calibration settings
          handleCalibrationSettings(request);
          break;
          
        case 'R': // Run calibration
          handleRunCalibration();
          break;

        case 'M': // Re-enable disabled faders
          handleEnableDisabledFaders();
          break;
          
        case 'D': // Debug mode toggle
          handleDebugToggle(requestBody); // Pass the request body instead of full request
          break;
          
        case 'H': // Home/Root page
          handleRoot();
          break;

        case 'E': // EEPROM dump
          dumpEepromConfig();
          sendRedirect();
          break;
        
        case 'F': // Fader settings
          handleFaderSettings(request);
          break;

        case 'V': // LED settings save
          handleLEDSettingsSave(request);
          break;

        case 'T': // Touch settings
          handleTouchSettings(request);
          break;

        case 'X': // Reset to defaults
          handleResetDefaults();
          break;

        case 'Z': // Reset network settings
          handleNetworkReset();
          break;
          
        case 'Y': // Stats JSON data
          handleStatsData();
          break;

        case 'S': // Stats page
          handleStatsPage();
          break;
          
        case 'G': // Fader settings page
          handleFaderSettingsPage();
          break;
          
        case 'L': // LED settings page
          handleLEDSettingsPage();
          break;
          
        case 'A': // OSC settings page
          handleOSCSettingsPage();
          break;
        case 'J':
          if (path == "/debug_settings") {
            handleDebugSettingsPage();
          } else {
            handleDebugSettings(request);
          }
          break;

        case 'W': // XML download
          handleGMA3ShortcutsDownload();
          break;

        case 'B': // Reboot
          handleRebootRequest();
          break;
        case 'I': // Favicon
          handleFavicon();
          break;
          
        default: // 404 or unrecognized request
          WEB_ERROR_PRINT("Unrecognized request, sending 404");
          send404Response();
          break;
      }
    } else {
      // Malformed request
      send404Response();
    }
    
    delay(10); // Give the web browser time to receive the data
    client.stop();
  WEB_DEBUG_PRINT("Client disconnected");
  }
}

void handleRebootRequest() {
  sendMessagePage("Rebooting", "Device is rebooting. You will be reconnected shortly.", "/", 15);
  delay(1500);
  resetTeensy();
}

//================================
// INDIVIDUAL REQUEST HANDLERS
//================================

void send404Response() {
  safePrintLn("HTTP/1.1 404 Not Found");
  safePrintLn("Content-Type: text/html");
  safePrintLn("Connection: close");
  safePrintLn();
  safePrintLn("<html><head>");
  safePrintLn("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  safePrintLn(F("<link rel='icon' type='image/svg+xml' href='data:image/svg+xml,%3Csvg%20xmlns%3D%22http://www.w3.org/2000/svg%22%20viewBox%3D%220%200%2032%2032%22%20width%3D%2232%22%20height%3D%2232%22%3E%3Cg%20stroke%3D%22%23000%22%20stroke-width%3D%22.7%22%20fill%3D%22%23ff7a00%22%3E%3Ccircle%20cx%3D%228%22%20cy%3D%226%22%20r%3D%222%22/%3E%3Crect%20x%3D%226.5%22%20y%3D%229%22%20width%3D%223%22%20height%3D%223%22%20rx%3D%22.8%22%20ry%3D%22.8%22%20fill%3D%22%23222%22/%3E%3Crect%20x%3D%226.5%22%20y%3D%2212%22%20width%3D%223%22%20height%3D%2214%22%20rx%3D%221%22%20ry%3D%221%22%20fill%3D%22none%22/%3E%3Crect%20x%3D%226.5%22%20y%3D%2219%22%20width%3D%223%22%20height%3D%224%22%20rx%3D%221%22%20ry%3D%221%22/%3E%3Ccircle%20cx%3D%2216%22%20cy%3D%226%22%20r%3D%222%22/%3E%3Crect%20x%3D%2214.5%22%20y%3D%229%22%20width%3D%223%22%20height%3D%223%22%20rx%3D%22.8%22%20ry%3D%22.8%22%20fill%3D%22%23222%22/%3E%3Crect%20x%3D%2214.5%22%20y%3D%2212%22%20width%3D%223%22%20height%3D%2214%22%20rx%3D%221%22%20ry%3D%221%22%20fill%3D%22none%22/%3E%3Crect%20x%3D%2214.5%22%20y%3D%2217%22%20width%3D%223%22%20height%3D%224%22%20rx%3D%221%22%20ry%3D%221%22/%3E%3Ccircle%20cx%3D%2224%22%20cy%3D%226%22%20r%3D%222%22/%3E%3Crect%20x%3D%2222.5%22%20y%3D%229%22%20width%3D%223%22%20height%3D%223%22%20rx%3D%22.8%22%20ry%3D%22.8%22%20fill%3D%22%23222%22/%3E%3Crect%20x%3D%2222.5%22%20y%3D%2212%22%20width%3D%223%22%20height%3D%2214%22%20rx%3D%221%22%20ry%3D%221%22%20fill%3D%22none%22/%3E%3Crect%20x%3D%2222.5%22%20y%3D%2221%22%20width%3D%223%22%20height%3D%224%22%20rx%3D%221%22%20ry%3D%221%22/%3E%3C/g%3E%3C/svg%3E'>"));
  safePrintLn("<style>");
  safePrintLn("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #202325; color: #e8e6e3; }");
  safePrintLn(".error-container { background: #181a1b; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.3); max-width: 500px; margin: 50px auto; text-align: center; border: 1px solid #3a3e41; }");
  safePrintLn("h1 { color: #f44336; margin-top: 0; font-size: 72px; margin-bottom: 10px; }");
  safePrintLn("h2 { color: #e8e6e3; margin-top: 0; }");
  safePrintLn("p { color: #a8a095; line-height: 1.6; }");
  safePrintLn("a { color: #3391ff; text-decoration: none; font-weight: 500; }");
  safePrintLn("a:hover { text-decoration: underline; }");
  safePrintLn("</style></head><body>");
  safePrintLn("<div class='error-container'>");
  safePrintLn("<h1>404</h1>");
  safePrintLn("<h2>Page Not Found</h2>");
  safePrintLn("<p>The requested resource was not found on this server.</p>");
  safePrintLn("<p><a href='/'>Return to home</a></p>");
  safePrintLn("</div></body></html>");
}


void handleDebugToggle(String requestBody) {
  WEB_DEBUG_PRINT("Legacy /debug toggle request received.");
  bool enableSerialDebug = (requestBody.indexOf("debug=1") != -1 || requestBody.indexOf("debug=on") != -1);
  Fconfig.serialDebug = enableSerialDebug;
  debugMode = enableSerialDebug;
  saveFaderConfig();
  if (!debugMode) {
    networkRequestReconfigure();
  }
  if (!debugMode) display.clearDebugLines();
  if (!debugMode) displayIPAddress();
  sendMessagePage("Debug Setting Saved", "Debug output setting has been updated.", "/debug_settings", 2);
}

void handleDebugSettings(String request) {
  WEB_DEBUG_PRINT("Handling debug settings...");

  bool hasMasterField = (request.indexOf("serialDebug_present=1") >= 0);
  if (hasMasterField) {
    bool enableSerialDebug = (request.indexOf("serialDebug=on") >= 0 || request.indexOf("serialDebug=1") >= 0);
    Fconfig.serialDebug = enableSerialDebug;
    debugMode = enableSerialDebug;
  }

  Fconfig.debugConfigVersion = DEBUG_CONFIG_VERSION;
  for (uint8_t i = 0; i < DEBUG_CHANNEL_COUNT; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "dbg%u", static_cast<unsigned>(i));
    String levelStr = getParam(request, key);
    if (levelStr.length() == 0) {
      continue;
    }

    int level = levelStr.toInt();
    if (level < DBG_OFF) {
      level = DBG_OFF;
    } else if (level > DBG_DEBUG) {
      level = DBG_DEBUG;
    }
    Fconfig.debugLevel[i] = static_cast<uint8_t>(level);
  }

  saveFaderConfig();
  if (!debugMode) {
    networkRequestReconfigure();
  }
  if (!debugMode) display.clearDebugLines();
  if (!debugMode) displayIPAddress();
  sendMessagePage("Debug Settings Saved", "Debug channel settings have been saved.", "/debug_settings", 2);
}

void handleFavicon() {
  const char* svgData = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\" width=\"32\" height=\"32\"><g stroke=\"#000\" stroke-width=\".7\" fill=\"#ff7a00\"><circle cx=\"8\" cy=\"6\" r=\"2\"/><rect x=\"6.5\" y=\"9\" width=\"3\" height=\"3\" rx=\".8\" ry=\".8\" fill=\"#222\"/><rect x=\"6.5\" y=\"12\" width=\"3\" height=\"14\" rx=\"1\" ry=\"1\" fill=\"none\"/><rect x=\"6.5\" y=\"19\" width=\"3\" height=\"4\" rx=\"1\" ry=\"1\"/><circle cx=\"16\" cy=\"6\" r=\"2\"/><rect x=\"14.5\" y=\"9\" width=\"3\" height=\"3\" rx=\".8\" ry=\".8\" fill=\"#222\"/><rect x=\"14.5\" y=\"12\" width=\"3\" height=\"14\" rx=\"1\" ry=\"1\" fill=\"none\"/><rect x=\"14.5\" y=\"17\" width=\"3\" height=\"4\" rx=\"1\" ry=\"1\"/><circle cx=\"24\" cy=\"6\" r=\"2\"/><rect x=\"22.5\" y=\"9\" width=\"3\" height=\"3\" rx=\".8\" ry=\".8\" fill=\"#222\"/><rect x=\"22.5\" y=\"12\" width=\"3\" height=\"14\" rx=\"1\" ry=\"1\" fill=\"none\"/><rect x=\"22.5\" y=\"21\" width=\"3\" height=\"4\" rx=\"1\" ry=\"1\"/></g></svg>";
  safePrintLn("HTTP/1.1 200 OK");
  safePrintLn("Content-Type: image/svg+xml");
  safePrint("Content-Length: ");
  safePrintLn(strlen(svgData));
  safePrintLn("Cache-Control: public, max-age=86400");
  safePrintLn("Connection: close");
  safePrintLn();
  safePrint(svgData);
}

void handleNetworkSettings(String request) {
  WEB_DEBUG_PRINT("Handling network settings...");
  
  // FIXED: Proper DHCP checkbox detection
  bool newDHCP = (request.indexOf("dhcp=on") >= 0 || request.indexOf("dhcp=1") >= 0);
  
  // Extract network parameters with validation
  String ipStr = getParam(request, "ip");
  String gwStr = getParam(request, "gw");
  String snStr = getParam(request, "sn");
  
  // Validate IP addresses before applying
  IPAddress newStaticIP = stringToIP(ipStr);
  IPAddress newGateway = stringToIP(gwStr);
  IPAddress newSubnet = stringToIP(snStr);
  
  // Only update if we have valid values
  if (ipStr.length() > 0 && isValidIP(newStaticIP)) {
    netConfig.staticIP = newStaticIP;
    WEB_DEBUG_PRINTF("Updated Static IP: %s", ipToString(netConfig.staticIP).c_str());
  } else if (ipStr.length() > 0) {
    WEB_ERROR_PRINTF("Invalid static IP: %s", ipStr.c_str());
    sendErrorResponse("Invalid static IP address");
    return;
  }
  
  if (gwStr.length() > 0 && isValidIP(newGateway)) {
    netConfig.gateway = newGateway;
  } else if (gwStr.length() > 0) {
    WEB_ERROR_PRINTF("Invalid gateway: %s", gwStr.c_str());
    sendErrorResponse("Invalid gateway address");
    return;
  }
  
  if (snStr.length() > 0 && isValidIP(newSubnet)) {
    netConfig.subnet = newSubnet;
  } else if (snStr.length() > 0) {
    WEB_ERROR_PRINTF("Invalid subnet: %s", snStr.c_str());
    sendErrorResponse("Invalid subnet address");
    return;
  }
  
  // Update DHCP setting
  netConfig.useDHCP = newDHCP;
  WEB_DEBUG_PRINTF("DHCP setting: %s", netConfig.useDHCP ? "ENABLED" : "DISABLED");
  
  // Save to EEPROM
  saveNetworkConfig();
  sendMessagePage("Network Settings Saved", "Network settings have been saved. For changes to take full effect, you have to restart the device.", "/");

}



void handleOSCSettingsPage() {
  safePrintLn(F("HTTP/1.1 200 OK"));
  safePrintLn(F("Content-Type: text/html"));
  safePrintLn(F("Connection: close"));
  safePrintLn();
  
  safePrintLn(F("<!DOCTYPE html><html><head><title>OSC Settings</title>"));
  safePrintLn(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  safePrintLn(F("</head><body>"));
  
  sendNavigationHeader("OSC Settings");
  
  safePrintLn(F("<div class='container'>"));
  
  waitForWriteSpace(400);
  
  safePrint(F("<div class='card'><h2>OSC Settings</h2><form method='get' action='/save'><input type='hidden' name='saveScope' value='osc_settings'>"
              "<label>OSC Send IP</label><input type='text' name='osc_sendip' value='"));
  safePrint(ipToString(netConfig.sendToIP));
  safePrint(F("'><p class='help'>IP address of GMA3 console</p>"
              "<label>OSC Port</label><input type='number' name='osc_port' value='"));
  safePrint(netConfig.oscPort);
  safePrint(F("'>"
              "<hr style='border:0;border-top:1px solid #2d3133;margin:14px 0;'>"
              "<label>"));
  safePrint(F("<input type='checkbox' name='sendKeystrokes' value='on'"));
  if (Fconfig.sendKeystrokes) safePrint(F(" checked"));
  safePrintLn(F("> Send USB Keystrokes instead of OSC for Exec keys</label>"
                "<p class='help'>*must have usb plugged in, allows a more native experience with the ability to store directly using the physical keys, must use keyboard shortcuts XML file</p>"
                "<button type='submit'>Save OSC Settings</button></form></div>"));

  waitForWriteSpace(400);

  waitForWriteSpace(400);
  safePrintLn(F("<div class='card'><h2>Downloads</h2>"
                "<p><strong>GMA3 Keyboard Shortcuts XML</strong></p>"
                "<p class='help'>Import this XML file into GMA3 to set up keyboard shortcuts. Use this when 'Send USB Keystrokes' is enabled above.</p>"
                "<form method='get' action='/downloadshortcuts'>"
                "<button type='submit'>Download GMA3 Shortcuts XML</button>"
                "</form></div>"));

  safePrintLn(F("</div>"));

  waitForWriteSpace(600);
  sendFooter();
  safePrintLn(F("</body></html>"));
}

void handleCalibrationSettings(String request) {
  WEB_DEBUG_PRINT("Handling calibration settings...");
  
  String calibPwmStr = getParam(request, "calib_pwm");
  
  if (calibPwmStr.length() > 0) {
    int calibPwm = calibPwmStr.toInt();
    
    // Use constrainParam for validation
    Fconfig.calibratePwm = constrainParam(calibPwm, 0, 255, Fconfig.calibratePwm);
    WEB_DEBUG_PRINTF("Calibration PWM saved: %d", Fconfig.calibratePwm);
    
    // Save to EEPROM
    saveFaderConfig();
    
    sendMessagePage("Calibration Saved", "Calibration speed has been saved successfully.", "/fader_settings", 3);
  } else {
    sendErrorResponse("Missing calibration PWM parameter");
    return;
  }
}

void handleFaderSettings(String request) {
  WEB_DEBUG_PRINT("Handling fader settings...");
  
  // Extract parameter strings
  String minPwmStr = getParam(request, "minPwm");
  String maxPwmStr = getParam(request, "maxPwm");
  String targetToleranceStr = getParam(request, "targetTolerance");
  String sendToleranceStr = getParam(request, "sendTolerance");
  String slowZoneStr = getParam(request, "slowZone");
  String fastZoneStr = getParam(request, "fastZone");
  bool hasAllowOscWithoutTouchField = (request.indexOf("allowOscWithoutTouch_present=1") >= 0);
  bool allowOscWithoutTouch = (request.indexOf("allowOscWithoutTouch=on") >= 0 ||
                               request.indexOf("allowOscWithoutTouch=1") >= 0);
  
  // Validate and update using constrainParam
  if (minPwmStr.length() > 0) {
    int minPwm = minPwmStr.toInt();
    Fconfig.minPwm = constrainParam(minPwm, 0, 255, Fconfig.minPwm);
  }
  
  if (maxPwmStr.length() > 0) {
    int maxPwm = maxPwmStr.toInt();
    Fconfig.maxPwm = constrainParam(maxPwm, 0, 255, Fconfig.maxPwm);
  }
  
  if (targetToleranceStr.length() > 0) {
    int targetTolerance = targetToleranceStr.toInt();
    Fconfig.targetTolerance = constrainParam(targetTolerance, 0, 100, Fconfig.targetTolerance);
  }
  
  if (slowZoneStr.length() > 0) {
    int slowZoneVal = slowZoneStr.toInt();
    Fconfig.slowZone = constrainParam(slowZoneVal, 0, 100, Fconfig.slowZone);
  }

  if (fastZoneStr.length() > 0) {
    int fastZoneVal = fastZoneStr.toInt();
    Fconfig.fastZone = constrainParam(fastZoneVal, 0, 100, Fconfig.fastZone);
  }

  // Ensure ordering: fastZone must be greater than slowZone
  if (Fconfig.fastZone <= Fconfig.slowZone) {
    // Reset to defaults when user input is invalid (e.g., both 0 or both 100)
    Fconfig.slowZone = SLOW_ZONE;
    Fconfig.fastZone = FAST_ZONE;
  }

  if (sendToleranceStr.length() > 0) {
    int sendTolerance = sendToleranceStr.toInt();
    Fconfig.sendTolerance = constrainParam(sendTolerance, 0, 100, Fconfig.sendTolerance);
  }

  if (hasAllowOscWithoutTouchField) {
    Fconfig.allowFaderOscWithoutTouch = allowOscWithoutTouch;
  }
  
  // Additional logical validation
  if (Fconfig.minPwm > Fconfig.maxPwm) {
    WEB_ERROR_PRINT("Min PWM is greater than Default PWM, swapping values");
    int temp = Fconfig.minPwm;
    Fconfig.minPwm = Fconfig.maxPwm;
    Fconfig.maxPwm = temp;
  }

  // Save to EEPROM
  saveFaderConfig();
  
  sendMessagePage("Fader Settings Saved", "Fader settings have been saved successfully.", "/fader_settings", 3);
}

void handleLEDSettingsSave(String request) {
  WEB_DEBUG_PRINT("Handling LED settings...");

  String baseBrightnessStr = getParam(request, "bb");
  String touchedBrightnessStr = getParam(request, "tb");
  String fadeTimeStr = getParam(request, "ft");
  bool newUseLevelPixels = (request.indexOf("lp=on") >= 0 || request.indexOf("lp=1") >= 0);
  String execBaseBrightnessStr = getParam(request, "eb");
  String execActiveBrightnessStr = getParam(request, "ea");
  bool newUseStaticColor = (request.indexOf("sc=on") >= 0 || request.indexOf("sc=1") >= 0);
  String staticRedStr = getParam(request, "sr");
  String staticGreenStr = getParam(request, "sg");
  String staticBlueStr = getParam(request, "sb");
  String staticColorHex = getParam(request, "sch");

  int parsedStaticR = execConfig.staticRed;
  int parsedStaticG = execConfig.staticGreen;
  int parsedStaticB = execConfig.staticBlue;

  // If a color picker value is present, use it as the starting point
  uint8_t pickerR, pickerG, pickerB;
  if (parseHexColor(staticColorHex, pickerR, pickerG, pickerB)) {
    parsedStaticR = pickerR;
    parsedStaticG = pickerG;
    parsedStaticB = pickerB;
  }

  if (baseBrightnessStr.length() > 0) {
    int baseBrightness = baseBrightnessStr.toInt();
    Fconfig.baseBrightness = constrainParam(baseBrightness, 0, 255, Fconfig.baseBrightness);
    updateBaseBrightnessPixels();
    WEB_DEBUG_PRINTF("Base Brightness saved: %d", Fconfig.baseBrightness);
  }

  if (touchedBrightnessStr.length() > 0) {
    int touchedBrightness = touchedBrightnessStr.toInt();
    Fconfig.touchedBrightness = constrainParam(touchedBrightness, 0, 255, Fconfig.touchedBrightness);
    WEB_DEBUG_PRINTF("Touched Brightness saved: %d", Fconfig.touchedBrightness);
  }

  if (fadeTimeStr.length() > 0) {
    int fadeTime = fadeTimeStr.toInt();
    Fconfig.fadeTime = constrainParam(fadeTime, 0, 10000, Fconfig.fadeTime);
    WEB_DEBUG_PRINTF("Fade Time saved: %d", Fconfig.fadeTime);
  }

  bool ledModeChanged = (Fconfig.useLevelPixels != newUseLevelPixels);
  Fconfig.useLevelPixels = newUseLevelPixels;
  WEB_DEBUG_PRINTF("Use Level Pixels: %s", Fconfig.useLevelPixels ? "true" : "false");

  if (execBaseBrightnessStr.length() > 0) {
    int execBase = execBaseBrightnessStr.toInt();
    execConfig.baseBrightness = constrainParam(execBase, 0, 255, execConfig.baseBrightness);
    WEB_DEBUG_PRINTF("Exec Base Brightness saved: %d", execConfig.baseBrightness);
  }

  if (execActiveBrightnessStr.length() > 0) {
    int execActive = execActiveBrightnessStr.toInt();
    execConfig.activeBrightness = constrainParam(execActive, 0, 255, execConfig.activeBrightness);
    WEB_DEBUG_PRINTF("Exec Active Brightness saved: %d", execConfig.activeBrightness);
  }

  if (staticRedStr.length() > 0) {
    parsedStaticR = constrainParam(staticRedStr.toInt(), 0, 255, parsedStaticR);
  }
  if (staticGreenStr.length() > 0) {
    parsedStaticG = constrainParam(staticGreenStr.toInt(), 0, 255, parsedStaticG);
  }
  if (staticBlueStr.length() > 0) {
    parsedStaticB = constrainParam(staticBlueStr.toInt(), 0, 255, parsedStaticB);
  }

  execConfig.useStaticColor = newUseStaticColor;
  execConfig.staticRed = parsedStaticR;
  execConfig.staticGreen = parsedStaticG;
  execConfig.staticBlue = parsedStaticB;

  WEB_DEBUG_PRINTF("Exec Static Color Enabled: %s", execConfig.useStaticColor ? "true" : "false");
  WEB_DEBUG_PRINTF("Exec Static Color: R%d G%d B%d", execConfig.staticRed, execConfig.staticGreen, execConfig.staticBlue);

  if (ledModeChanged) {
    invalidateNeoPixelRenderCache();
    updateNeoPixels();
  }

  saveFaderConfig();
  saveExecConfig();
  markKeyLedsDirty();
  sendMessagePage("LED Settings Saved", "LED settings have been saved successfully.", "/led_settings", 3);
}

void handleRunCalibration() {
  WEB_DEBUG_PRINT("Running fader calibration...");

  sendMessagePage("Fader calibration started", "Redirecting to statistics page...", "/stats", 2);
  
  // Run the calibration process
  calibrateFaders();
  saveCalibration();

  // Recalibrate touch sensor after motor calibration
  WEB_DEBUG_PRINT("Recalibrating touch sensor after fader calibration...");
  runTouchCalibration();

  // Inform user and redirect to statistics page
  
}

void handleEnableDisabledFaders() {
  WEB_DEBUG_PRINT("Re-enabling disabled faders...");

  int reEnabledCount = 0;
  for (int i = 0; i < NUM_FADERS; i++) {
    if (!faders[i].motorEnabled) {
      faders[i].motorEnabled = true;
      faders[i].failureCount = 0;
      faders[i].lastFailureTime = 0;
      reEnabledCount++;
    }
  }

  if (reEnabledCount > 0) {
    sendMessagePage("Disabled faders enabled", "Previously disabled fader motors were re-enabled.", "/stats", 2);
  } else {
    sendMessagePage("No disabled faders", "No fader motors were disabled.", "/stats", 2);
  }
}

void handleTouchSettings(String request) {
  WEB_DEBUG_PRINT("Handling touch sensor settings...");
  
  String autoCalModeStr = getParam(request, "autoCalMode");
  String touchThresholdStr = getParam(request, "touchThreshold");
  String releaseThresholdStr = getParam(request, "releaseThreshold");
  
  // Validate and update using constrainParam
  if (autoCalModeStr.length() > 0) {
    int autoCalMode = autoCalModeStr.toInt();
    autoCalibrationMode = constrainParam(autoCalMode, 0, 1, autoCalibrationMode);
  }
  
  if (touchThresholdStr.length() > 0) {
    int threshold = touchThresholdStr.toInt();
    touchThreshold = constrainParam(threshold, 1, 255, touchThreshold);
  }
  
  if (releaseThresholdStr.length() > 0) {
    int threshold = releaseThresholdStr.toInt();
#if defined(TOUCH_SENSOR_MTCH2120)
    releaseThreshold = constrainParam(threshold, 0, 7, releaseThreshold); // Hysteresis code 0-7
#else
    releaseThreshold = constrainParam(threshold, 1, 255, releaseThreshold);
#endif
  }
  
#if defined(TOUCH_SENSOR_MPR121)
  // Additional logical validation - ensure release < touch for MPR121 semantics
  if (releaseThreshold >= touchThreshold) {
    WEB_ERROR_PRINT("Release threshold >= touch threshold, adjusting");
    releaseThreshold = touchThreshold - 1;
    if (releaseThreshold < 1) {
      releaseThreshold = 1;
      touchThreshold = 2;
    }
  }
#endif
  
  // Apply the settings to the touch sensor
  setAutoTouchCalibration(autoCalibrationMode);
  manualTouchCalibration();
  
  fadeSequence(25,500);
  
  // Save to EEPROM
  saveTouchConfig();
  
  sendMessagePage("Touch Settings Saved", "Touch settings have been saved successfully.", "/fader_settings", 3);
}

void handleResetDefaults() {
  WEB_DEBUG_PRINT("Resetting all settings to defaults...");
  resetToDefaults();
  sendMessagePage("Factory Defaults Restored", "All settings have been reset to factory defaults.", "/", 3);
}

void handleOSCSettings(String request) {
  WEB_DEBUG_PRINT("Handling OSC settings only...");
  
  // Extract OSC parameters
  String sendIPStr = getParam(request, "osc_sendip");
  String oscPortStr = getParam(request, "osc_port");
  
  // NEW: Extract sendKeystrokes checkbox
  bool newSendKeystrokes = (request.indexOf("sendKeystrokes=on") >= 0 || request.indexOf("sendKeystrokes=1") >= 0);
  
  // Validate and update OSC Send IP
  if (sendIPStr.length() > 0) {
    IPAddress newSendIP = stringToIP(sendIPStr);
    if (isValidIP(newSendIP)) {
      netConfig.sendToIP = newSendIP;
      WEB_DEBUG_PRINTF("Updated OSC Send IP: %s", ipToString(netConfig.sendToIP).c_str());
    } else {
      WEB_ERROR_PRINTF("Invalid OSC send IP: %s", sendIPStr.c_str());
      sendErrorResponse("Invalid OSC send IP address");
      return;
    }
  }
  
  // Validate and update shared OSC port
  if (oscPortStr.length() > 0) {
    int newOscPort = oscPortStr.toInt();
    if (isValidPort(newOscPort)) {
      netConfig.oscPort = newOscPort;
      WEB_DEBUG_PRINTF("Updated OSC Port: %d", netConfig.oscPort);
    } else {
      WEB_ERROR_PRINTF("Invalid OSC port: %d", newOscPort);
      sendErrorResponse("Invalid OSC port (must be 1-65535)");
      return;
    }
  }
  
  // NEW: Update sendKeystrokes setting
  Fconfig.sendKeystrokes = newSendKeystrokes;
  WEB_DEBUG_PRINTF("Updated sendKeystrokes: %s", Fconfig.sendKeystrokes ? "true" : "false");

  // Save both network config (for OSC settings) and fader config (for sendKeystrokes)
  saveNetworkConfig();
  saveFaderConfig();  // NEW: Save fader config for sendKeystrokes setting

  WEB_DEBUG_PRINT("OSC settings saved successfully");
  sendMessagePage("OSC Settings Saved", "OSC settings have been saved successfully. Network services will refresh automatically if needed.", "/osc_settings", 3);
}


void handleNetworkReset() {  
  WEB_DEBUG_PRINT("Resetting network settings to defaults...");
  sendMessagePage("Network Settings Reset",
                  "Network settings are resetting. For changes to take full effect reboot device.",
                  "/");
  resetNetworkDefaults();
}

void sendRedirect() {
  safePrintLn("HTTP/1.1 303 See Other");
  safePrintLn("Location: /");
  safePrintLn("Connection: close");
  safePrintLn();
}

// Helper function to send CSS styles
void sendCommonStylesLight() {
  safePrintLn("<style>");
  safePrintLn("body { font-family: Arial, sans-serif; margin: 0; padding: 0; background: #f0f0f0; }");
  safePrintLn(".header { background: #1976d2; color: white; padding: 20px; text-align: center; }");
  safePrintLn(".header h1 { margin: 0; font-size: 24px; }");
  safePrintLn(".header p { margin: 5px 0; font-size: 14px; }");
  safePrintLn(".nav { background: #333; padding: 10px; text-align: center; }");
  safePrintLn(".nav a { color: white; text-decoration: none; padding: 5px 15px; margin: 0 5px; }");
  safePrintLn(".nav a:hover { background: #555; }");
  safePrintLn(".container { max-width: 600px; margin: 20px auto; padding: 0 20px; }");
  safePrintLn(".card { background: white; padding: 20px; margin-bottom: 20px; border: 1px solid #ddd; }");
  safePrintLn(".card h2 { margin-top: 0; font-size: 20px; border-bottom: 1px solid #ddd; padding-bottom: 10px; }");
  safePrintLn("input[type='text'], input[type='number'], select { width: 100%; padding: 8px; margin: 5px 0; box-sizing: border-box; }");
  safePrintLn("label { display: block; margin-top: 10px; font-weight: bold; }");
  safePrintLn(".help { font-size: 12px; color: #666; margin-top: 2px; }");
  safePrintLn("button { background: #1976d2; color: white; padding: 10px 20px; border: none; cursor: pointer; width: 100%; margin-top: 10px; }");
  safePrintLn("button:hover { background: #1565c0; }");
  safePrintLn(".divider { border-top: 1px solid #ddd; margin: 20px 0; }");
  safePrintLn("</style>");
}

// Helper function to send CSS styles
void sendCommonStyles() {
  safePrintLn(F("<link rel='icon' type='image/svg+xml' href='/favicon.svg'>"));
  waitForWriteSpace(800);
  safePrintLn("<style>");
  safePrintLn("body { margin: 0; font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif; background: #181a1b; color: #e8e6e3; }");
  safePrintLn(".container { max-width: 800px; margin: 20px auto; padding: 0 16px; display: flex; flex-direction: column; gap: 16px; }");
  safePrintLn(".logo-section { text-align: center; padding: 16px 0 8px; }");
  safePrintLn(".logo-svg { width: 240px; height: auto; display: block; margin: 0 auto; }");
  safePrintLn(".ip-bar { text-align: center; color: #a8a095; font-size: 13px; margin-bottom: 8px; }");
  safePrintLn(".nav-links { display: flex; justify-content: center; gap: 8px; flex-wrap: wrap; margin: 0 auto 18px; padding: 0 12px; width: 100%; max-width: 800px; box-sizing: border-box; }");
  safePrintLn(".nav-links a { color: #e8e6e3; text-decoration: none; padding: 10px 14px; background: #222425; border-radius: 10px 10px 0 0; font-weight: 600; }");
  safePrintLn(".nav-links a:hover { background: #2f3234; }");
  safePrintLn(".nav-links a.active { background: #ff7a00; color: #0f0f0f; }");
  safePrintLn(".card { background: #202324; padding: 18px; margin-bottom: 16px; border: 1px solid #2d3133; border-radius: 10px; box-sizing: border-box; }");
  safePrintLn(".card h2 { margin: 0 0 12px; font-size: 20px; border-bottom: 1px solid #2d3133; padding-bottom: 8px; }");
  safePrintLn(".card-body { display: flex; flex-direction: column; gap: 10px; }");
  safePrintLn(".form-group { margin-bottom: 6px; }");
  safePrintLn("label { display: block; margin: 10px 0 4px; font-weight: 600; color: #e8e6e3; }");
  safePrintLn("input[type='text'], input[type='number'], select { width:100%; padding: 10px; margin: 6px 0; box-sizing: border-box; background: #1b1d1e; color: #e8e6e3; border: 1px solid #3a3e41; border-radius: 6px; }");
  safePrintLn(".help, .help-text { font-size: 12px; color: #a8a095; margin-top: 4px; }");
  safePrintLn("button, .btn { display: block; width:200px; background: #ff7a00; color: #0f0f0f; padding: 11px; border: none; cursor: pointer; border-radius: 6px; font-weight: 700; margin: 12px auto 0; text-align: center; }");
  safePrintLn("button:hover, .btn:hover { background: #e56a00; }");
  safePrintLn(".divider { border-top: 1px solid #3a3e41; margin: 18px 0; }");
  safePrintLn(".color-row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }");
  safePrintLn(".color-row input[type='color'] { flex: 1 0 140px; min-height: 44px; padding: 0; border: 1px solid #3a3e41; border-radius: 6px; background: #1b1d1e; }");
  safePrintLn(".color-row .channel-input { flex: 1 0 70px; width: auto; }");
  safePrintLn("</style>");
}


// Helper function to send navigation header
void sendNavigationHeader(const char* pageTitle) {
  safePrintLn("<div class='logo-section'>");
  safePrintLn("<svg class='logo-svg' xmlns='http://www.w3.org/2000/svg' viewBox='0 0 520 320'><text x='215' y='200' text-anchor='end' font-family='DejaVu Sans, Arial, Helvetica, sans-serif' font-weight='700' font-size='110' fill='#ff7a00'>Evo</text><text x='430' y='280' text-anchor='end' font-family='DejaVu Sans, Arial, Helvetica, sans-serif' font-weight='700' font-size='80' fill='#ff7a00'>FaderWing</text><g class='fader-bank' stroke='#000'><circle cx='242' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='230' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='230' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='230' y='160' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='282' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='270' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='270' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='270' y='154' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='322' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='310' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='310' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='310' y='145' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='362' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='350' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='350' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='350' y='168' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='402' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='390' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='390' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='390' y='150' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/></g></svg>");


  bool isNetwork = strcmp(pageTitle, "Network Settings") == 0;
  bool isOsc = strcmp(pageTitle, "OSC Settings") == 0 || strcmp(pageTitle, "OSC") == 0;
  bool isFader = strcmp(pageTitle, "Fader Configuration") == 0 || strcmp(pageTitle, "Faders") == 0;
  bool isLED = strcmp(pageTitle, "LED Settings") == 0 || strcmp(pageTitle, "LEDs") == 0;
  bool isStats = strcmp(pageTitle, "Statistics") == 0;
  bool isDebug = strcmp(pageTitle, "Debug Settings") == 0;

  safePrintLn("<div class='nav-links'>");

  safePrint("<a href='/'");
  if (isNetwork) safePrint(" class='active'");
  safePrintLn(">Network</a>");

  safePrint("<a href='/osc_settings'");
  if (isOsc) safePrint(" class='active'");
  safePrintLn(">OSC</a>");

  safePrint("<a href='/fader_settings'");
  if (isFader) safePrint(" class='active'");
  safePrintLn(">Faders</a>");

  safePrint("<a href='/led_settings'");
  if (isLED) safePrint(" class='active'");
  safePrintLn(">LEDs</a>");

  safePrint("<a href='/stats'");
  if (isStats) safePrint(" class='active'");
  safePrintLn(">Statistics</a>");

  safePrint("<a href='/debug_settings'");
  if (isDebug) safePrint(" class='active'");
  safePrintLn(">Debug</a>");
  safePrintLn("</div>");

  // Ip bar
  safePrintLn("</div><div class='ip-bar'>");
  safePrint("OSC Send: ");
  safePrint(ipToString(netConfig.sendToIP));
  safePrint(":");
  safePrint(netConfig.oscPort);
  safePrint(" | OSC Local: ");
  safePrint(ipToString(networkGetLocalIP()));
  safePrint(":");
  safePrint(netConfig.oscPort);
  safePrint(" | Key Send Mode: ");
  safePrint(Fconfig.sendKeystrokes ? "USB" : "OSC");
  safePrintLn("</div>");


}

void sendFooter() {
  safePrintLn(F("<div class='ip-bar'>V" SW_VERSION " - by Shawn R</div>"));
}

void sendMessagePage(const char* title, const char* message, const char* redirectUrl, int redirectSeconds) {
  safePrintLn("HTTP/1.1 200 OK");
  safePrintLn("Content-Type: text/html");
  safePrintLn("Connection: close");
  safePrintLn();
  safePrintLn("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>");
  safePrintLn("<link rel='icon' type='image/svg+xml' href='/favicon.svg'>");
  if (redirectUrl && redirectUrl[0] != '\0' && redirectSeconds > 0) {
    safePrint("<script>setTimeout(function(){ window.location.replace('");
    safePrint(redirectUrl);
    safePrint("'); }, ");
    safePrint(redirectSeconds * 1000);
    safePrintLn(");</script>");
  }
  safePrintLn("<style>");
  safePrintLn("body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif; background: #181a1b; color: #e8e6e3; margin: 0; padding: 20px; }");
  safePrintLn(".msg-container { max-width: 520px; margin: 60px auto; background: #202324; border: 1px solid #2d3133; border-radius: 10px; padding: 24px 20px; box-shadow: 0 2px 6px rgba(0,0,0,0.35); text-align: center; }");
  safePrintLn(".msg-container h1 { margin: 0 0 12px; font-size: 24px; color: #ff7a00; }");
  safePrintLn(".msg-container p { margin: 8px 0; color: #a8a095; line-height: 1.5; }");
  safePrintLn(".msg-container a { color: #ff7a00; text-decoration: none; font-weight: 700; }");
  safePrintLn(".msg-container a:hover { text-decoration: underline; }");
  safePrintLn("</style></head><body>");
  safePrintLn("<div class='msg-container'>");
  safePrint("<h1>");
  safePrint(title);
  safePrintLn("</h1>");
  safePrint("<p>");
  safePrint(message);
  safePrintLn("</p>");
  if (redirectUrl && redirectUrl[0] != '\0') {
    safePrint("<p><a href='");
    safePrint(redirectUrl);
    safePrintLn("'>Continue</a></p>");
  }
  safePrintLn("</div></body></html>");
}

static const char* debugLevelLabel(uint8_t level) {
  switch (level) {
    case DBG_OFF: return "OFF";
    case DBG_ERROR: return "ERROR";
    case DBG_DEBUG: return "DEBUG";
    default: return "ERROR";
  }
}

static void sendDebugChannelSelect(DebugChannel channel, const char* helpText = nullptr) {
  uint8_t idx = static_cast<uint8_t>(channel);
  uint8_t selected = Fconfig.debugLevel[idx];
  if (selected > DBG_DEBUG) {
    selected = DBG_ERROR;
  }

  char fieldName[12];
  snprintf(fieldName, sizeof(fieldName), "dbg%u", static_cast<unsigned>(idx));

  safePrint("<div class='form-group'><label>");
  safePrint(debugChannelName(channel));
  safePrint("</label><select name='");
  safePrint(fieldName);
  safePrint("'>");

  for (uint8_t value = DBG_OFF; value <= DBG_DEBUG; ++value) {
    safePrint("<option value='");
    safePrint(value);
    safePrint("'");
    if (selected == value) {
      safePrint(" selected");
    }
    safePrint(">");
    safePrint(debugLevelLabel(value));
    safePrint("</option>");
  }
  safePrint("</select>");

  if (helpText != nullptr && helpText[0] != '\0') {
    safePrint("<p class='help-text'>");
    safePrint(helpText);
    safePrint("</p>");
  }

  safePrintLn("</div>");
}

void handleStatsData() {
  safePrintLn(F("HTTP/1.1 200 OK"));
  safePrintLn(F("Content-Type: application/json"));
  safePrintLn(F("Cache-Control: no-cache, no-store, must-revalidate"));
  safePrintLn(F("Connection: close"));
  safePrintLn();

  safePrint(F("{\"faders\":["));
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    int oscVal = readFadertoOSC(f);
    int currentVal = (f.lastAnalogValue >= 0) ? f.lastAnalogValue : 0;

    if (i > 0) safePrint(',');
    safePrint(F("{\"id\":"));
    safePrint(i + 1);
    safePrint(F(",\"current\":"));
    safePrint(currentVal);
    safePrint(F(",\"min\":"));
    safePrint(f.minVal);
    safePrint(F(",\"max\":"));
    safePrint(f.maxVal);
    safePrint(F(",\"osc\":"));
    safePrint(oscVal);
    safePrint(F(",\"motorEnabled\":"));
    safePrint(f.motorEnabled ? F("true") : F("false"));
    safePrint('}');

    if (i % 3 == 0) waitForWriteSpace(200);
  }
  safePrintLn(F("]}"));
}


void handleStatsPage() {
  safePrintLn("HTTP/1.1 200 OK");
  safePrintLn("Content-Type: text/html");
  safePrintLn("Connection: close");
  safePrintLn();
  
  safePrintLn("<!DOCTYPE html><html><head><title>Fader Statistics</title>");
  safePrintLn("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  sendCommonStyles();
  safePrintLn("<style>");
  safePrintLn("table { width: 100%; border-collapse: collapse; }");
  safePrintLn("th, td { border: 1px solid #3a3e41; padding: 8px; text-align: left; }");
  safePrintLn("th { background: #ff7a00; color: #0f0f0f; }");
  safePrintLn("</style>");
  safePrintLn("</head><body>");
  
  sendNavigationHeader("Statistics");
  
  safePrintLn("<div class='container'>");
  safePrintLn("<div class='card'>");
  safePrintLn("<h2>Live Fader Stats</h2>");
  safePrintLn(
    "<div style='display:flex;gap:12px;flex-wrap:wrap;margin-bottom:16px;'>"
    "<form method='post' action='/enable_disabled_faders' style='margin:0;'>"
    "<button type='submit' class='btn btn-warning'>Enable Disabled Faders</button>"
    "</form>"
    "<form method='post' action='/calibrate' style='margin:0;'>"
    "<input type='hidden' name='calibrate' value='1'>"
    "<button type='submit' class='btn btn-info'>Calibrate Faders</button>"
    "</form>"
    "</div>");
  
  safePrintLn("<table id='stats-table'>");
  safePrintLn("<tr><th>Fader</th><th>Current</th><th>Min</th><th>Max</th><th>OSC Value</th></tr>");
  safePrintLn("<tbody id='stats-body'><tr><td colspan='5'>Loading...</td></tr></tbody></table>");

  safePrintLn("</div>");
  safePrintLn("</div>");
  waitForWriteSpace(600);
  safePrintLn(F("<script>"
    "const statsBody=document.getElementById('stats-body');"
    "function renderStats(data){if(!data||!data.faders)return;"
    "let rows='';"
    "for(let i=0;i<data.faders.length;i++){const f=data.faders[i];"
    "const label=f.motorEnabled?`Fader ${f.id}`:`Fader ${f.id} [DISABLED]`;"
    "rows+=`<tr><td>${label}</td><td>${f.current}</td><td>${f.min}</td><td>${f.max}</td><td>${f.osc}</td></tr>`;}"
    "statsBody.innerHTML=rows;}"
    "async function refreshStats(){try{const res=await fetch('/stats_data');if(!res.ok)return;const data=await res.json();renderStats(data);}catch(e){}}"
    "refreshStats();"
    "setInterval(refreshStats,500);"
    "</script>"));
  sendFooter();
  safePrintLn("</body></html>");
}

void handleFaderSettingsPage() {
  safePrintLn(F("HTTP/1.1 200 OK"));
  safePrintLn(F("Content-Type: text/html"));
  safePrintLn(F("Connection: close"));
  safePrintLn();

  safePrintLn(F("<!DOCTYPE html><html><head><title>Fader Configuration</title>"));
  safePrintLn(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  safePrintLn(F("</head><body>"));

  sendNavigationHeader("Fader Configuration");

  safePrintLn(F("<div class='container'>"));

  waitForWriteSpace(600);

  safePrint(F(
    "<div class='card'><div class='card-header'><h2>Fader Settings</h2></div><div class='card-body'><form method='get' action='/save'><input type='hidden' name='saveScope' value='fader_speed'>"
    "<div class='form-group'><label>Min Speed</label><input type='number' name='minPwm' value='"));
  safePrint(Fconfig.minPwm);
  safePrint(F(
    "' min='0' max='255'><p class='help-text'>Too low stalls motor, too high passes setpoint and causes jitter) (0-255)</p></div>"
    "<div class='form-group'><label>Max Speed</label><input type='number' name='maxPwm' value='"));
  safePrint(Fconfig.maxPwm);
  safePrint(F(
    "' min='0' max='255'><p class='help-text'>Max motor speed (0-255)</p></div>"
    "<div class='form-group'><label>Slow Speed Zone</label><input type='number' name='slowZone' value='"));
  safePrint(Fconfig.slowZone);
  safePrint(F(
    "' min='0' max='100'><p class='help-text'>Fader runs at min speed when nearer than this distance to the setpoint.</p></div>"
    "<div class='form-group'><label>Fast Speed Zone</label><input type='number' name='fastZone' value='"));
  safePrint(Fconfig.fastZone);
  
  waitForWriteSpace(600);

  safePrint(F(
    "' min='1' max='100'><p class='help-text'>Fader runs at max speed when farther than this distance from the setpoint.</p></div>"
    "<p class='help-text'>Between these distances, speed scales smoothly from min to max.</p>"
    "<div class='divider'></div>"
    "<div class='form-group'><label>Target Tolerance</label><input type='number' name='targetTolerance' value='"));
  safePrint(Fconfig.targetTolerance);
  safePrintLn(F(
    "' min='0' max='100'><p class='help-text'>Position accuracy before motor stops</p></div>"));

  waitForWriteSpace(600);

  safePrint(F("<div class='form-group'><label>Send Tolerance</label><input type='number' name='sendTolerance' value='"));
  safePrint(Fconfig.sendTolerance);
  safePrintLn(F(
    "' min='0' max='100'><p class='help-text'>Min movement before OSC update <2 can cause jitter</p></div>"
    "<input type='hidden' name='allowOscWithoutTouch_present' value='1'>"
    "<div class='form-group'><label><input type='checkbox' name='allowOscWithoutTouch' value='on'"));
  if (Fconfig.allowFaderOscWithoutTouch) safePrint(F(" checked"));
  safePrintLn(F(
    "> Send OSC from fader movement even when touch is not detected</label>"
    "<p class='help-text'>Useful when touch sensitivity or touch response timing misses a fast move.</p>"
    "<p class='help-text'>Disable this if you notice OSC values bouncing or fighting due to feedback loops.</p></div>"
    "<button type='submit' class='btn btn-primary btn-block'>Save Fader Settings</button>"
    "</form></div></div>"));

  waitForWriteSpace(600);

  safePrint(F(
    "<div class='card' style='margin-top: 20px;'><div class='card-header'><h2>Calibration & Touch</h2></div><div class='card-body'>"
    "<form method='get' action='/save'><input type='hidden' name='saveScope' value='calibration_fader'><div class='form-group'><label>Motor Calibration Speed</label><input type='number' name='calib_pwm' value='"));
  safePrint(Fconfig.calibratePwm);
  safePrintLn(F(
    "' min='0' max='255'><p class='help-text'>Motor speed during calibration (lower = gentler)</p></div><button type='submit' class='btn btn-success btn-block'>Save Calibration Speed</button></form>"
    "<form method='post' action='/calibrate'><input type='hidden' name='calibrate' value='1'><button type='submit' class='btn btn-info btn-block'>Run Fader Calibration</button></form>"
    "<p class='help-text'>Calibration also clears any disabled fader motors.</p>"
    "<div class='divider'></div>"
    "<form method='get' action='/save'><input type='hidden' name='saveScope' value='calibration_touch'><h3 style='margin: 0 0 10px;'>Touch Sensor</h3>"));

#if defined(TOUCH_SENSOR_MTCH2120)
  safePrint(F("<div class='form-group'><label>Auto Calibration (AutoTune)</label><select name='autoCalMode'>"
#else
  safePrint(F("<div class='form-group'><label>Auto Calibration</label><select name='autoCalMode'>"
#endif
    "<option value='0'"));
  if (autoCalibrationMode == 0) safePrint(F(" selected"));
  safePrint(F(">Disabled</option><option value='1'"));
  if (autoCalibrationMode == 1) safePrint(F(" selected"));
  safePrintLn(F(">Enabled</option></select>"
#if defined(TOUCH_SENSOR_MTCH2120)
    "<p class='help-text'>MTCH2120 AutoTune baseline tracking. Leave enabled unless troubleshooting.</p>"
#else
    "<p class='help-text'>Toggles the built-in autoconfig for baselines. Disabled leaves power-up defaults.</p>"
#endif
    "</div>"));

  safePrint(F("<div class='form-group'><label>Touch Threshold</label><input type='number' name='touchThreshold' value='"));
  safePrint(touchThreshold);
  safePrintLn(F("' min='1' max='255'><p class='help-text'>Higher values = less sensitive"
#if defined(TOUCH_SENSOR_MTCH2120)
    " (default: 128)"
#else
    " (default: 12)"
#endif
    "</p></div>"));

#if defined(TOUCH_SENSOR_MTCH2120)
  safePrint(F("<div class='form-group'><label>Hysteresis</label><input type='number' name='releaseThreshold' value='"));
  safePrint(releaseThreshold);
  safePrintLn(F("' min='0' max='7'><p class='help-text'>MTCH2120 HYS code (0-7). 1 = ~25% (default), higher = stickier releases.</p></div>"));
#else
  safePrint(F("<div class='form-group'><label>Release Threshold</label><input type='number' name='releaseThreshold' value='"));
  safePrint(releaseThreshold);
  safePrintLn(F("' min='1' max='255'><p class='help-text'>Lower values = harder to release (default: 6)</p></div>"));
#endif

  safePrintLn(F(
    "<button type='submit' class='btn btn-primary btn-block'>Save Touch Settings</button>"
    "<p class='help-text' style='margin-top: 12px; color: red;'>Do not touch faders while saving</p>"
    "</form></div></div>"));

  waitForWriteSpace(800);
  safePrintLn(F("</div>")); // container
  sendFooter();
  safePrintLn(F("</body></html>"));
}

void handleLEDSettingsPage() {
  safePrintLn(F("HTTP/1.1 200 OK"));
  safePrintLn(F("Content-Type: text/html"));
  safePrintLn(F("Connection: close"));
  safePrintLn();
  
  safePrintLn(F("<!DOCTYPE html><html><head><title>Fader LEDs</title>"));
  safePrintLn(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  safePrintLn(F("</head><body>"));
  
  sendNavigationHeader("LED Settings");
  
  safePrintLn(F("<div class='container'>"));
  waitForWriteSpace(600);
  
  safePrint(F("<div class='card'><div class='card-header'><h2>Fader LEDs</h2></div><div class='card-body'><form method='get' action='/save'><input type='hidden' name='saveScope' value='led_fader'>"
              "<div class='form-group'><label>Base Level</label><input type='number' name='bb' value='"));
  safePrint(Fconfig.baseBrightness);
  safePrint(F("' min='0' max='255'><p class='help-text'>LED brightness when fader is not touched (0-255)</p></div>"
              "<div class='form-group'><label>Touched Level</label><input type='number' name='tb' value='"));
  safePrint(Fconfig.touchedBrightness);
  safePrint(F("' min='0' max='255'><p class='help-text'>LED brightness when fader is touched (0-255)</p></div>"
              "<div class='form-group'><label>Fade Time</label><input type='number' name='ft' value='"));
  safePrint(Fconfig.fadeTime);
  safePrintLn(F("' min='0' max='10000'><p class='help-text'>Time in ms that the LEDs will fade</p></div>"
                "<div class='form-group'><label>LED Mode</label><label style='display: inline-block; margin-top: 6px;'>"
                "<input type='checkbox' name='lp' value='on'"));
  if (Fconfig.useLevelPixels) safePrint(F(" checked"));
  
  waitForWriteSpace(800);

  safePrint(F("> Show level bars instead of full fill</label><p class='help-text'>When enabled the fader lights up to match the position.</p></div>"
              "<div class='divider'></div>"
              "<h3 style='margin: 6px 0;'>Exec LEDs</h3>"
              "<div class='form-group'><label>Off Level</label><input type='number' name='eb' value='"));
  safePrint((int)execConfig.baseBrightness);
  safePrint(F("' min='0' max='255'><p class='help-text'>Level when populated/off.</p></div>"
              "<div class='form-group'><label>On Level</label><input type='number' name='ea' value='"));
  safePrint((int)execConfig.activeBrightness);
  safePrint(F("' min='0' max='255'><p class='help-text'>Level when active/on.</p></div>"
              "<div class='form-group'><label><input type='checkbox' name='sc' value='on'"));
  if (execConfig.useStaticColor) safePrint(F(" checked"));
  safePrint(F("> Use Static Color</label><p class='help-text'>Static color overrides appearance.</p></div>"
              "<div class='form-group'><label>Static Color</label><div class='color-row'>"
              "<input type='color' id='staticColorPicker' name='sch' value='"));

  waitForWriteSpace(600);

  char staticHex[8];
  snprintf(staticHex, sizeof(staticHex), "#%02X%02X%02X", execConfig.staticRed, execConfig.staticGreen, execConfig.staticBlue);
  safePrint(staticHex);
  safePrint(F("'><input type='number' class='channel-input' id='staticRed' name='sr' min='0' max='255' value='"));
  safePrint((int)execConfig.staticRed);
  safePrint(F("'><input type='number' class='channel-input' id='staticGreen' name='sg' min='0' max='255' value='"));
  safePrint((int)execConfig.staticGreen);
  safePrint(F("'><input type='number' class='channel-input' id='staticBlue' name='sb' min='0' max='255' value='"));
  safePrint((int)execConfig.staticBlue);
  safePrintLn(F("'></div><p class='help-text'>Static picker or RGB values (0-255).</p></div>"
                "<button type='submit' class='btn btn-primary btn-block'>Save LED Settings</button>"
                "</form></div></div>"));
  
  waitForWriteSpace(600);
  safePrintLn(F("</div>")); // container

  safePrintLn(F("<script>"
                "function clampChannel(v){v=parseInt(v);if(isNaN(v))return 0;return Math.min(255,Math.max(0,v));}"
                "function toHex(v){const h=clampChannel(v).toString(16);return h.length===1?'0'+h:h;}"
                "function syncPicker(){const r=document.getElementById('staticRed');const g=document.getElementById('staticGreen');const b=document.getElementById('staticBlue');"
                "document.getElementById('staticColorPicker').value='#'+toHex(r.value)+toHex(g.value)+toHex(b.value);}"
                "function syncInputs(){const hex=document.getElementById('staticColorPicker').value||'#000000';"
                "document.getElementById('staticRed').value=parseInt(hex.substr(1,2),16);"
                "document.getElementById('staticGreen').value=parseInt(hex.substr(3,2),16);"
                "document.getElementById('staticBlue').value=parseInt(hex.substr(5,2),16);}"
                "document.getElementById('staticColorPicker').addEventListener('input',syncInputs);"
                "['staticRed','staticGreen','staticBlue'].forEach(id=>{const el=document.getElementById(id);if(el){el.addEventListener('input',syncPicker);}});"
                "syncPicker();"
                "</script>"));

  waitForWriteSpace(800);
  
  sendFooter();
  safePrintLn(F("</body></html>"));
}

void handleDebugSettingsPage() {
  safePrintLn(F("HTTP/1.1 200 OK"));
  safePrintLn(F("Content-Type: text/html"));
  safePrintLn(F("Connection: close"));
  safePrintLn();

  safePrintLn(F("<!DOCTYPE html><html><head><title>Debug Settings</title>"));
  safePrintLn(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  safePrintLn(F("</head><body>"));

  sendNavigationHeader("Debug Settings");

  safePrintLn(F("<div class='container'>"));

  safePrintLn(F("<div class='card'><h2>System Reset</h2>"
                "<p class='help-text'>These actions affect the whole device.</p>"
                "<form method='post' action='/reset_defaults'>"
                "<button type='submit' onclick=\"return confirm('Reset ALL settings?');\">Reset All Settings</button>"
                "</form>"
                "<form method='post' action='/reboot'>"
                "<button type='submit' onclick=\"return confirm('Reboot EvoFaderWing?');\">Reboot</button>"
                "</form>"
                "</div>"));

  waitForWriteSpace(500);

  safePrintLn(F("<form method='get' action='/save'>"
                "<input type='hidden' name='saveScope' value='debug_settings'>"
                "<input type='hidden' name='serialDebug_present' value='1'>"));

  safePrintLn(F("<div class='card'><h2>Debug Control</h2>"
                "<label><input type='checkbox' name='serialDebug' value='on'"));
  if (Fconfig.serialDebug) safePrint(F(" checked"));
  safePrintLn(F("> Enable Serial Debug Output</label>"
                "<p class='help-text'>Master gate for all debug channels. If off, all channel output is suppressed.</p>"
                "<button type='submit' class='btn btn-primary btn-block'>Save Debug Settings</button>"
                "<button type='submit' formaction='/dump' formmethod='post'>Dump EEPROM to Serial</button>"
                "</div>"));

  waitForWriteSpace(600);

  safePrintLn(F("<div class='card'><h2>Debug Channels</h2>"
                "<div style='display:flex;gap:8px;flex-wrap:wrap;margin:10px 0 14px 0;'>"
                "<button type='button' onclick='setAllDebugLevels(0)' style='flex:1;min-width:110px;'>All Off</button>"
                "<button type='button' onclick='setAllDebugLevels(2)' style='flex:1;min-width:110px;'>All Debug</button>"
                "<button type='button' onclick='setAllDebugLevels(1)' style='flex:1;min-width:110px;'>All Error</button>"
                "</div>"));
  safePrintLn(F("<p class='help-text'>Core</p>"));
  sendDebugChannelSelect(DBG_CH_SYSTEM);
  sendDebugChannelSelect(DBG_CH_WEB);
  sendDebugChannelSelect(DBG_CH_NETWORK);
  sendDebugChannelSelect(DBG_CH_OSC);

  safePrintLn(F("<div class='divider'></div><p class='help-text'>I2C</p>"));
  sendDebugChannelSelect(DBG_CH_I2C_BUS);

  safePrintLn(F("<div class='divider'></div><p class='help-text'>Motion/Fader</p>"));
  sendDebugChannelSelect(DBG_CH_FADER_CORE);
  sendDebugChannelSelect(DBG_CH_FADER_POSITION, "Logs outbound fader OSC sends so you can confirm movement/output.");
  sendDebugChannelSelect(DBG_CH_CALIBRATION);

  safePrintLn(F("<div class='divider'></div><p class='help-text'>Touch</p>"));
  sendDebugChannelSelect(DBG_CH_TOUCH_CORE);
  sendDebugChannelSelect(DBG_CH_TOUCH_RAW, "High-rate stream. Enable only for sensor-level diagnostics.");

  safePrintLn(F("<div class='divider'></div><p class='help-text'>Storage</p>"));
  sendDebugChannelSelect(DBG_CH_EEPROM);

  safePrintLn(F("<div class='divider'></div><p class='help-text'>Visual</p>"));
  sendDebugChannelSelect(DBG_CH_LED_EXEC);
  sendDebugChannelSelect(DBG_CH_OLED);
  safePrintLn(F("</div>"));

  waitForWriteSpace(400);

  safePrintLn(F("<button type='submit' class='btn btn-primary btn-block'>Save Debug Settings</button>"
                "</form>"));

  safePrintLn(F("</div>"));
  safePrintLn(F("<script>"
                "function setAllDebugLevels(level){"
                "document.querySelectorAll(\"select[name^='dbg']\").forEach(function(el){el.value=String(level);});"
                "}"
                "</script>"));
  sendFooter();
  safePrintLn(F("</body></html>"));
}

//================================
// MAIN WEB PAGE HANDLER (ROOT - Network Settings)
//================================
void handleRoot() {
  safePrintLn("HTTP/1.1 200 OK");
  safePrintLn("Content-Type: text/html");
  safePrintLn("Connection: close");
  safePrintLn();
  safePrintLn("<!DOCTYPE html><html><head><title>Network Settings</title>");
  safePrintLn("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  sendCommonStyles();
  safePrintLn("</head><body>");

  sendNavigationHeader("Network Settings");

  safePrintLn(F("<div class='container'>"));
  waitForWriteSpace(400);

  // Network Settings Card
  safePrintLn(F("<div class='card'><h2>Network Settings</h2><form method='get' action='/save'><input type='hidden' name='saveScope' value='network_settings'>"));
  safePrint(F("<label><input type='checkbox' name='dhcp' value='on'"));
  if (netConfig.useDHCP) safePrint(F(" checked"));
  safePrintLn(F("> Use DHCP</label><p class='help'>When enabled, static IP settings below are ignored</p>"));

  safePrint(F("<label>Static IP Address</label><input type='text' name='ip' value='"));
  safePrint(ipToString(netConfig.staticIP));
  safePrintLn(F("'>"));

  safePrint(F("<label>Gateway</label><input type='text' name='gw' value='"));
  safePrint(ipToString(netConfig.gateway));
  safePrintLn(F("'>"));

  safePrint(F("<label>Subnet Mask</label><input type='text' name='sn' value='"));
  safePrint(ipToString(netConfig.subnet));
  safePrintLn(F("'>"));

  safePrintLn(F("<button type='submit'>Save Network Settings</button></form>"));
  safePrintLn(F("<form method='post' action='/reset_network'>"
                "<button type='submit' onclick=\"return confirm('Reset network settings?');\">Reset Network</button>"
                "</form></div>"));

  safePrintLn(F("</div>")); // End container
  sendFooter();
  safePrintLn(F("</body></html>"));
}

void waitForWriteSpace(size_t minBytes) {
  const unsigned long startMs = millis();
  constexpr unsigned long kWaitTimeoutMs = 2000;
  while (client.connected()) {
    int available = client.availableForWrite();
    if (available < 0 || static_cast<size_t>(available) >= minBytes) {
      break;
    }
    Ethernet.loop();
    delay(1);
    if (millis() - startMs > kWaitTimeoutMs) {
      WEB_ERROR_PRINT("waitForWriteSpace timeout; continuing response");
      break;
    }
  }
}



void handleGMA3ShortcutsDownload() {
  WEB_DEBUG_PRINT("Serving GMA3 shortcuts XML file download...");

  // Send headers for file download
  safePrintLn("HTTP/1.1 200 OK");
  safePrintLn("Content-Type: application/xml");
  safePrintLn("Content-Disposition: attachment; filename=\"EvoFaderWing_keyboard_shortcuts.xml\"");
  safePrintLn("Connection: close");
  safePrintLn();

  safePrintLn(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
  safePrintLn(F("<GMA3 DataVersion=\"1.9.3.3\">"));
  safePrintLn(F("    <KeyboardShortCuts KeyboardShortcutsActive=\"Yes\">"));

  const KeyMapping* keyMap = getKeyMap();
  const int keyMapSize = getKeyMapSize();
  int currentRow = -1;

  for (int i = 0; i < keyMapSize; ++i) {
    const int execIndex = keyMap[i].executorIndex;
    const int row = execIndex / 100;

    if (row != currentRow) {
      if (i != 0) {
        safePrintLn();
      }
      safePrint(F("        <!-- Row "));
      safePrint(row);
      safePrint(F(" ("));
      safePrint(row);
      safePrint(F("01-"));
      safePrint(row);
      safePrintLn(F("10) -->"));
      currentRow = row;
    }

    safePrint(F("        <KeyboardShortcut Lock=\"Yes\" KeyCode=\"EXEC\" ExecutorIndex=\""));
    safePrint(execIndex);
    safePrint(F("\" Shortcut=\""));
    safePrint(keyMap[i].keyName);
    safePrintLn(F("\" />"));
  }

  safePrintLn(F("    </KeyboardShortCuts>"));
  safePrint(F("</GMA3>"));
}
