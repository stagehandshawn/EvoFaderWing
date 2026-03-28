// WebServer.cpp - Memory Optimized Version with Separate OSC Page

#include "WebServer.h"
#include "Utils.h"
#include "EEPROMStorage.h"
#include "FaderControl.h"
#include "TouchSensor.h"
#include <QNEthernet.h>
#include "NeoPixelControl.h"
#include "OLED.h"
#include "NetworkOSC.h"
#include "KeyLedControl.h"
#include <stdio.h>
#include <stdlib.h>

using namespace qindesign::network;


//================================
// GLOBAL NETWORK OBJECTS
//================================
EthernetServer server(80);
EthernetClient client;

//================================
// SERVER MANAGEMENT
//================================

void startWebServer() {
  server.begin();
  debugPrint("Web server started at http://");
  debugPrint(ipToString(Ethernet.localIP()).c_str());
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
    debugPrintf("Warning: Value %d out of range [%d-%d], using default %d\n", 
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
  client.println("HTTP/1.1 400 Bad Request");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<link rel='icon' type='image/svg+xml' href='/favicon.svg'>");
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #202325; color: #e8e6e3; }");
  client.println(".error-container { background: #181a1b; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.3); max-width: 500px; margin: 50px auto; border: 1px solid #3a3e41; }");
  client.println("h1 { color: #f44336; margin-top: 0; }");
  client.println("p { color: #a8a095; line-height: 1.6; }");
  client.println("a { color: #3391ff; text-decoration: none; font-weight: 500; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='error-container'>");
  client.println("<h1>Error</h1>");
  client.printf("<p>%s</p>", errorMsg);
  client.println("<p><a href='/'>Return to settings</a></p>");
  client.println("</div></body></html>");
}

//================================
// MAIN REQUEST HANDLER
//================================

void handleWebServer() {
  if (client) {
    debugPrint("New client connected");
    
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
            debugPrintf("Content-Length: %d\n", contentLength);
          }
        }
        
        // Check for end of headers
        if (!headersDone && request.endsWith("\r\n\r\n")) {
          headersDone = true;
          debugPrint("Headers complete, reading body...");
          
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
            
            debugPrintf("Request body (%d bytes): %s\n", requestBody.length(), requestBody.c_str());
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
      
      debugPrintf("Request: %s %s\n", method.c_str(), path.c_str());
      
      // Determine which type of request to handle
      char requestType = '\0';
      
      // UPDATED ROUTING LOGIC
      if (path.startsWith("/save")) {
        debugPrint("Processing /save request");
        debugPrintf("Request parameters: %s\n", request.c_str());
        
        // Check for specific parameter combinations to determine request type
        // Use more specific patterns to avoid false matches (e.g., "ip=" inside "osc_sendip=")
        bool hasNetworkFields = (request.indexOf("&ip=") >= 0 || request.indexOf("?ip=") >= 0 || 
                                request.indexOf("dhcp=") >= 0 || 
                                request.indexOf("gw=") >= 0 || request.indexOf("sn=") >= 0);
        bool hasOSCFields = (request.indexOf("osc_sendip=") >= 0 || request.indexOf("osc_sendport=") >= 0 || 
                            request.indexOf("osc_receiveport=") >= 0);
        
        // Debug output to see what's being detected
        debugPrintf("hasNetworkFields: %s\n", hasNetworkFields ? "true" : "false");
        debugPrintf("hasOSCFields: %s\n", hasOSCFields ? "true" : "false");
        debugPrintf("osc_sendip check: %d\n", request.indexOf("osc_sendip="));
        debugPrintf("osc_sendport check: %d\n", request.indexOf("osc_sendport="));
        debugPrintf("osc_receiveport check: %d\n", request.indexOf("osc_receiveport="));
        
        if (hasNetworkFields && !hasOSCFields) {
          requestType = 'N'; // Network settings only
          debugPrint("Determined: Network settings");
        } else if (hasOSCFields && !hasNetworkFields) {
          requestType = 'O'; // OSC settings only  
          debugPrint("Determined: OSC settings");
        } else if (hasOSCFields && hasNetworkFields) {
          // This should not happen with the new separated forms, but handle it
          debugPrint("WARNING: Both network and OSC fields detected - treating as OSC");
          requestType = 'O'; // Default to OSC to be safe
        } else if (request.indexOf("calib_pwm=") >= 0) {
          requestType = 'C'; // Calibration settings
          debugPrint("Determined: Calibration settings");
        } else if (request.indexOf("touchThreshold=") >= 0) {
          requestType = 'T'; // Touch settings
          debugPrint("Determined: Touch settings");
        } else if (request.indexOf("bb=") >= 0 || request.indexOf("tb=") >= 0 || 
                   request.indexOf("ft=") >= 0 || request.indexOf("lp=") >= 0 ||
                   request.indexOf("eb=") >= 0 || request.indexOf("ea=") >= 0 ||
                   request.indexOf("sc=") >= 0 || request.indexOf("sch=") >= 0) {
          requestType = 'V'; // LED settings only
          debugPrint("Determined: LED settings");
        } else if (request.indexOf("minPwm=") >= 0) {
          requestType = 'F'; // Fader settings
          debugPrint("Determined: Fader settings");
        } else {
          debugPrint("ERROR: Could not determine request type");
          debugPrintf("Request: %s\n", request.c_str());
        }
      } else if (path == "/calibrate" && method == "POST") {
        requestType = 'R'; // Run calibration
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
          debugPrint("Unrecognized request, sending 404");
          send404Response();
          break;
      }
    } else {
      // Malformed request
      send404Response();
    }
    
    delay(10); // Give the web browser time to receive the data
    client.stop();
  debugPrint("Client disconnected");
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
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println(F("<link rel='icon' type='image/svg+xml' href='data:image/svg+xml,%3Csvg%20xmlns%3D%22http://www.w3.org/2000/svg%22%20viewBox%3D%220%200%2032%2032%22%20width%3D%2232%22%20height%3D%2232%22%3E%3Cg%20stroke%3D%22%23000%22%20stroke-width%3D%22.7%22%20fill%3D%22%23ff7a00%22%3E%3Ccircle%20cx%3D%228%22%20cy%3D%226%22%20r%3D%222%22/%3E%3Crect%20x%3D%226.5%22%20y%3D%229%22%20width%3D%223%22%20height%3D%223%22%20rx%3D%22.8%22%20ry%3D%22.8%22%20fill%3D%22%23222%22/%3E%3Crect%20x%3D%226.5%22%20y%3D%2212%22%20width%3D%223%22%20height%3D%2214%22%20rx%3D%221%22%20ry%3D%221%22%20fill%3D%22none%22/%3E%3Crect%20x%3D%226.5%22%20y%3D%2219%22%20width%3D%223%22%20height%3D%224%22%20rx%3D%221%22%20ry%3D%221%22/%3E%3Ccircle%20cx%3D%2216%22%20cy%3D%226%22%20r%3D%222%22/%3E%3Crect%20x%3D%2214.5%22%20y%3D%229%22%20width%3D%223%22%20height%3D%223%22%20rx%3D%22.8%22%20ry%3D%22.8%22%20fill%3D%22%23222%22/%3E%3Crect%20x%3D%2214.5%22%20y%3D%2212%22%20width%3D%223%22%20height%3D%2214%22%20rx%3D%221%22%20ry%3D%221%22%20fill%3D%22none%22/%3E%3Crect%20x%3D%2214.5%22%20y%3D%2217%22%20width%3D%223%22%20height%3D%224%22%20rx%3D%221%22%20ry%3D%221%22/%3E%3Ccircle%20cx%3D%2224%22%20cy%3D%226%22%20r%3D%222%22/%3E%3Crect%20x%3D%2222.5%22%20y%3D%229%22%20width%3D%223%22%20height%3D%223%22%20rx%3D%22.8%22%20ry%3D%22.8%22%20fill%3D%22%23222%22/%3E%3Crect%20x%3D%2222.5%22%20y%3D%2212%22%20width%3D%223%22%20height%3D%2214%22%20rx%3D%221%22%20ry%3D%221%22%20fill%3D%22none%22/%3E%3Crect%20x%3D%2222.5%22%20y%3D%2221%22%20width%3D%223%22%20height%3D%224%22%20rx%3D%221%22%20ry%3D%221%22/%3E%3C/g%3E%3C/svg%3E'>"));
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #202325; color: #e8e6e3; }");
  client.println(".error-container { background: #181a1b; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.3); max-width: 500px; margin: 50px auto; text-align: center; border: 1px solid #3a3e41; }");
  client.println("h1 { color: #f44336; margin-top: 0; font-size: 72px; margin-bottom: 10px; }");
  client.println("h2 { color: #e8e6e3; margin-top: 0; }");
  client.println("p { color: #a8a095; line-height: 1.6; }");
  client.println("a { color: #3391ff; text-decoration: none; font-weight: 500; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='error-container'>");
  client.println("<h1>404</h1>");
  client.println("<h2>Page Not Found</h2>");
  client.println("<p>The requested resource was not found on this server.</p>");
  client.println("<p><a href='/'>Return to home</a></p>");
  client.println("</div></body></html>");
}


void handleDebugToggle(String requestBody) {
  Serial.println("[Toggle] Received /debug POST request");
  Serial.printf("[Toggle] Raw body: %s\n", requestBody.c_str());

  debugMode = (requestBody.indexOf("debug=1") != -1);
  Serial.printf("[Toggle] Debug mode is now: %d\n", debugMode);

    Fconfig.serialDebug = debugMode;
  
    if (!debugMode) display.clearDebugLines();
  if (!debugMode) displayIPAddress();

  saveFaderConfig();

  sendMessagePage("Debug Setting Saved", "Debug output setting has been updated.", "/", 3);
}

void handleFavicon() {
  const char* svgData = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\" width=\"32\" height=\"32\"><g stroke=\"#000\" stroke-width=\".7\" fill=\"#ff7a00\"><circle cx=\"8\" cy=\"6\" r=\"2\"/><rect x=\"6.5\" y=\"9\" width=\"3\" height=\"3\" rx=\".8\" ry=\".8\" fill=\"#222\"/><rect x=\"6.5\" y=\"12\" width=\"3\" height=\"14\" rx=\"1\" ry=\"1\" fill=\"none\"/><rect x=\"6.5\" y=\"19\" width=\"3\" height=\"4\" rx=\"1\" ry=\"1\"/><circle cx=\"16\" cy=\"6\" r=\"2\"/><rect x=\"14.5\" y=\"9\" width=\"3\" height=\"3\" rx=\".8\" ry=\".8\" fill=\"#222\"/><rect x=\"14.5\" y=\"12\" width=\"3\" height=\"14\" rx=\"1\" ry=\"1\" fill=\"none\"/><rect x=\"14.5\" y=\"17\" width=\"3\" height=\"4\" rx=\"1\" ry=\"1\"/><circle cx=\"24\" cy=\"6\" r=\"2\"/><rect x=\"22.5\" y=\"9\" width=\"3\" height=\"3\" rx=\".8\" ry=\".8\" fill=\"#222\"/><rect x=\"22.5\" y=\"12\" width=\"3\" height=\"14\" rx=\"1\" ry=\"1\" fill=\"none\"/><rect x=\"22.5\" y=\"21\" width=\"3\" height=\"4\" rx=\"1\" ry=\"1\"/></g></svg>";
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/svg+xml");
  client.print("Content-Length: ");
  client.println(strlen(svgData));
  client.println("Cache-Control: public, max-age=86400");
  client.println("Connection: close");
  client.println();
  client.print(svgData);
}

void handleNetworkSettings(String request) {
  debugPrint("Handling network settings...");
  
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
    debugPrintf("Updated Static IP: %s\n", ipToString(netConfig.staticIP).c_str());
  } else if (ipStr.length() > 0) {
    debugPrintf("ERROR: Invalid static IP: %s\n", ipStr.c_str());
    sendErrorResponse("Invalid static IP address");
    return;
  }
  
  if (gwStr.length() > 0 && isValidIP(newGateway)) {
    netConfig.gateway = newGateway;
  } else if (gwStr.length() > 0) {
    debugPrintf("ERROR: Invalid gateway: %s\n", gwStr.c_str());
    sendErrorResponse("Invalid gateway address");
    return;
  }
  
  if (snStr.length() > 0 && isValidIP(newSubnet)) {
    netConfig.subnet = newSubnet;
  } else if (snStr.length() > 0) {
    debugPrintf("ERROR: Invalid subnet: %s\n", snStr.c_str());
    sendErrorResponse("Invalid subnet address");
    return;
  }
  
  // Update DHCP setting
  netConfig.useDHCP = newDHCP;
  debugPrintf("DHCP setting: %s\n", netConfig.useDHCP ? "ENABLED" : "DISABLED");
  
  // Save to EEPROM
  saveNetworkConfig();
  sendMessagePage("Network Settings Saved", "Network settings have been saved. For changes to take full effect, you have to restart the device.", "/");

}



void handleOSCSettingsPage() {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close"));
  client.println();
  
  client.println(F("<!DOCTYPE html><html><head><title>OSC Settings</title>"));
  client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  client.println(F("</head><body>"));
  
  sendNavigationHeader("OSC Settings");
  
  client.println(F("<div class='container'>"));
  
  waitForWriteSpace(400);
  
  // OSC settings card
  client.print(F("<div class='card'><h2>OSC Settings</h2><form method='get' action='/save'>"
                 "<label>OSC Send IP</label><input type='text' name='osc_sendip' value='"));
  client.print(ipToString(netConfig.sendToIP));
  client.print(F("'><p class='help'>IP address of GMA3 console</p>"
                 "<label>OSC Send Port</label><input type='number' name='osc_sendport' value='"));
  client.print(netConfig.sendPort);
  client.print(F("'>"
                 "<label>OSC Receive Port</label><input type='number' name='osc_receiveport' value='"));
  client.print(netConfig.receivePort);
  client.print(F("'>"
                 "<hr style='border:0;border-top:1px solid #2d3133;margin:14px 0;'>"
                 "<label>"));
  client.print(F("<input type='checkbox' name='sendKeystrokes' value='on'"));
  if (Fconfig.sendKeystrokes) client.print(F(" checked"));
  client.println(F("> Send USB Keystrokes instead of OSC for Exec keys</label>"
                   "<p class='help'>*must have usb plugged in, allows a more native experience with the ability to store directly using the physical keys, must use keyboard shortcuts XML file</p>"
                   "<button type='submit'>Save OSC Settings</button></form></div>"));

  waitForWriteSpace(400);

  // Downloads card (within same container for consistent width)
  waitForWriteSpace(400);
  client.println(F("<div class='card'><h2>Downloads</h2>"
                   "<p><strong>GMA3 Keyboard Shortcuts XML</strong></p>"
                   "<p class='help'>Import this XML file into GMA3 to set up keyboard shortcuts. Use this when 'Send USB Keystrokes' is enabled above.</p>"
                   "<form method='get' action='/downloadshortcuts'>"
                   "<button type='submit'>Download GMA3 Shortcuts XML</button>"
                   "</form></div>"));

  // Close main container
  client.println(F("</div>"));

  waitForWriteSpace(600);
  sendFooter();
  client.println(F("</body></html>"));
}

void handleCalibrationSettings(String request) {
  debugPrint("Handling calibration settings...");
  
  String calibPwmStr = getParam(request, "calib_pwm");
  
  if (calibPwmStr.length() > 0) {
    int calibPwm = calibPwmStr.toInt();
    
    // Use constrainParam for validation
    Fconfig.calibratePwm = constrainParam(calibPwm, 0, 255, Fconfig.calibratePwm);
    debugPrintf("Calibration PWM saved: %d\n", Fconfig.calibratePwm);
    
    // Save to EEPROM
    saveFaderConfig();
    
    sendMessagePage("Calibration Saved", "Calibration speed has been saved successfully.", "/fader_settings", 3);
  } else {
    sendErrorResponse("Missing calibration PWM parameter");
    return;
  }
}

void handleFaderSettings(String request) {
  debugPrint("Handling fader settings...");
  
  // Extract parameter strings
  String minPwmStr = getParam(request, "minPwm");
  String maxPwmStr = getParam(request, "maxPwm");
  String targetToleranceStr = getParam(request, "targetTolerance");
  String sendToleranceStr = getParam(request, "sendTolerance");
  String slowZoneStr = getParam(request, "slowZone");
  String fastZoneStr = getParam(request, "fastZone");
  
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
  
  // Additional logical validation
  if (Fconfig.minPwm > Fconfig.maxPwm) {
    debugPrint("Warning: Min PWM is greater than Default PWM, swapping values");
    int temp = Fconfig.minPwm;
    Fconfig.minPwm = Fconfig.maxPwm;
    Fconfig.maxPwm = temp;
  }

  // Save to EEPROM
  saveFaderConfig();
  
  sendMessagePage("Fader Settings Saved", "Fader settings have been saved successfully.", "/fader_settings", 3);
}

void handleLEDSettingsSave(String request) {
  debugPrint("Handling LED settings...");

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
    debugPrintf("Base Brightness saved: %d\n", Fconfig.baseBrightness);
  }

  if (touchedBrightnessStr.length() > 0) {
    int touchedBrightness = touchedBrightnessStr.toInt();
    Fconfig.touchedBrightness = constrainParam(touchedBrightness, 0, 255, Fconfig.touchedBrightness);
    debugPrintf("Touched Brightness saved: %d\n", Fconfig.touchedBrightness);
  }

  if (fadeTimeStr.length() > 0) {
    int fadeTime = fadeTimeStr.toInt();
    Fconfig.fadeTime = constrainParam(fadeTime, 0, 10000, Fconfig.fadeTime);
    debugPrintf("Fade Time saved: %d\n", Fconfig.fadeTime);
  }

  Fconfig.useLevelPixels = newUseLevelPixels;
  debugPrintf("Use Level Pixels: %s\n", Fconfig.useLevelPixels ? "true" : "false");

  if (execBaseBrightnessStr.length() > 0) {
    int execBase = execBaseBrightnessStr.toInt();
    execConfig.baseBrightness = constrainParam(execBase, 0, 255, execConfig.baseBrightness);
    debugPrintf("Exec Base Brightness saved: %d\n", execConfig.baseBrightness);
  }

  if (execActiveBrightnessStr.length() > 0) {
    int execActive = execActiveBrightnessStr.toInt();
    execConfig.activeBrightness = constrainParam(execActive, 0, 255, execConfig.activeBrightness);
    debugPrintf("Exec Active Brightness saved: %d\n", execConfig.activeBrightness);
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

  debugPrintf("Exec Static Color Enabled: %s\n", execConfig.useStaticColor ? "true" : "false");
  debugPrintf("Exec Static Color: R%d G%d B%d\n", execConfig.staticRed, execConfig.staticGreen, execConfig.staticBlue);

  saveFaderConfig();
  saveExecConfig();
  markKeyLedsDirty();
  sendMessagePage("LED Settings Saved", "LED settings have been saved successfully.", "/led_settings", 3);
}

void handleRunCalibration() {
  debugPrint("Running fader calibration...");

  sendMessagePage("Fader calibration started", "Redirecting to statistics page...", "/stats", 2);
  
  // Run the calibration process
  calibrateFaders();
  saveCalibration();

  // Recalibrate touch sensor after motor calibration
  debugPrint("Recalibrating touch sensor after fader calibration...");
  runTouchCalibration();

  // Inform user and redirect to statistics page
  
}

void handleTouchSettings(String request) {
  debugPrint("Handling touch sensor settings...");
  
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
    debugPrint("Warning: Release threshold >= touch threshold, adjusting");
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
  debugPrint("Resetting all settings to defaults...");
  resetToDefaults();
  sendMessagePage("Factory Defaults Restored", "All settings have been reset to factory defaults.", "/", 3);
}

void handleOSCSettings(String request) {
  debugPrint("Handling OSC settings only...");
  
  // Extract OSC parameters
  String sendIPStr = getParam(request, "osc_sendip");
  String sendPortStr = getParam(request, "osc_sendport");
  String receivePortStr = getParam(request, "osc_receiveport");
  
  // NEW: Extract sendKeystrokes checkbox
  bool newSendKeystrokes = (request.indexOf("sendKeystrokes=on") >= 0 || request.indexOf("sendKeystrokes=1") >= 0);
  
  // Validate and update OSC Send IP
  if (sendIPStr.length() > 0) {
    IPAddress newSendIP = stringToIP(sendIPStr);
    if (isValidIP(newSendIP)) {
      netConfig.sendToIP = newSendIP;
      debugPrintf("Updated OSC Send IP: %s\n", ipToString(netConfig.sendToIP).c_str());
    } else {
      debugPrintf("ERROR: Invalid OSC send IP: %s\n", sendIPStr.c_str());
      sendErrorResponse("Invalid OSC send IP address");
      return;
    }
  }
  
  // Validate and update OSC Send Port
  if (sendPortStr.length() > 0) {
    int newSendPort = sendPortStr.toInt();
    if (isValidPort(newSendPort)) {
      netConfig.sendPort = newSendPort;
      debugPrintf("Updated OSC Send Port: %d\n", netConfig.sendPort);
    } else {
      debugPrintf("ERROR: Invalid OSC send port: %d\n", newSendPort);
      sendErrorResponse("Invalid OSC send port (must be 1-65535)");
      return;
    }
  }
  
  // Validate and update OSC Receive Port
  if (receivePortStr.length() > 0) {
    int newReceivePort = receivePortStr.toInt();
    if (isValidPort(newReceivePort)) {
      netConfig.receivePort = newReceivePort;
      // NEED TO UPDATE UDP HANDLER
      debugPrintf("Updated OSC Receive Port: %d\n", netConfig.receivePort);
    } else {
      debugPrintf("ERROR: Invalid OSC receive port: %d\n", newReceivePort);
      sendErrorResponse("Invalid OSC receive port (must be 1-65535)");
      return;
    }
  }
  
  // NEW: Update sendKeystrokes setting
  Fconfig.sendKeystrokes = newSendKeystrokes;
  debugPrintf("Updated sendKeystrokes: %s\n", Fconfig.sendKeystrokes ? "true" : "false");

  // Save both network config (for OSC settings) and fader config (for sendKeystrokes)
  saveNetworkConfig();
  saveFaderConfig();  // NEW: Save fader config for sendKeystrokes setting

  debugPrint("OSC settings saved successfully");
  sendMessagePage("OSC Settings Saved", "OSC settings have been saved successfully. For changes to take full effect, you may have to restart the device.", "/osc_settings", 3);
}


void handleNetworkReset() {  
  debugPrint("Resetting network settings to defaults...");
  sendMessagePage("Network Settings Reset",
                  "Network settings are resetting. For changes to take full effect reboot device.",
                  "/");
  resetNetworkDefaults();
}

void sendRedirect() {
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}

// Helper function to send CSS styles
void sendCommonStylesLight() {
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; margin: 0; padding: 0; background: #f0f0f0; }");
  client.println(".header { background: #1976d2; color: white; padding: 20px; text-align: center; }");
  client.println(".header h1 { margin: 0; font-size: 24px; }");
  client.println(".header p { margin: 5px 0; font-size: 14px; }");
  client.println(".nav { background: #333; padding: 10px; text-align: center; }");
  client.println(".nav a { color: white; text-decoration: none; padding: 5px 15px; margin: 0 5px; }");
  client.println(".nav a:hover { background: #555; }");
  client.println(".container { max-width: 600px; margin: 20px auto; padding: 0 20px; }");
  client.println(".card { background: white; padding: 20px; margin-bottom: 20px; border: 1px solid #ddd; }");
  client.println(".card h2 { margin-top: 0; font-size: 20px; border-bottom: 1px solid #ddd; padding-bottom: 10px; }");
  client.println("input[type='text'], input[type='number'], select { width: 100%; padding: 8px; margin: 5px 0; box-sizing: border-box; }");
  client.println("label { display: block; margin-top: 10px; font-weight: bold; }");
  client.println(".help { font-size: 12px; color: #666; margin-top: 2px; }");
  client.println("button { background: #1976d2; color: white; padding: 10px 20px; border: none; cursor: pointer; width: 100%; margin-top: 10px; }");
  client.println("button:hover { background: #1565c0; }");
  client.println(".divider { border-top: 1px solid #ddd; margin: 20px 0; }");
  client.println("</style>");
}

// Helper function to send CSS styles
void sendCommonStyles() {
  client.println(F("<link rel='icon' type='image/svg+xml' href='/favicon.svg'>"));
  waitForWriteSpace(800);
  client.println("<style>");
  client.println("body { margin: 0; font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif; background: #181a1b; color: #e8e6e3; }");
  client.println(".container { max-width: 800px; margin: 20px auto; padding: 0 16px; display: flex; flex-direction: column; gap: 16px; }");
  client.println(".logo-section { text-align: center; padding: 16px 0 8px; }");
  client.println(".logo-svg { width: 240px; height: auto; display: block; margin: 0 auto; }");
  client.println(".ip-bar { text-align: center; color: #a8a095; font-size: 13px; margin-bottom: 8px; }");
  client.println(".nav-links { display: flex; justify-content: center; gap: 8px; flex-wrap: wrap; margin: 0 auto 18px; padding: 0 12px; width: 100%; max-width: 800px; box-sizing: border-box; }");
  client.println(".nav-links a { color: #e8e6e3; text-decoration: none; padding: 10px 14px; background: #222425; border-radius: 10px 10px 0 0; font-weight: 600; }");
  client.println(".nav-links a:hover { background: #2f3234; }");
  client.println(".nav-links a.active { background: #ff7a00; color: #0f0f0f; }");
  client.println(".card { background: #202324; padding: 18px; margin-bottom: 16px; border: 1px solid #2d3133; border-radius: 10px; box-sizing: border-box; }");
  client.println(".card h2 { margin: 0 0 12px; font-size: 20px; border-bottom: 1px solid #2d3133; padding-bottom: 8px; }");
  client.println(".card-body { display: flex; flex-direction: column; gap: 10px; }");
  client.println(".form-group { margin-bottom: 6px; }");
  client.println("label { display: block; margin: 10px 0 4px; font-weight: 600; color: #e8e6e3; }");
  client.println("input[type='text'], input[type='number'], select { width:100%; padding: 10px; margin: 6px 0; box-sizing: border-box; background: #1b1d1e; color: #e8e6e3; border: 1px solid #3a3e41; border-radius: 6px; }");
  client.println(".help, .help-text { font-size: 12px; color: #a8a095; margin-top: 4px; }");
  client.println("button, .btn { display: block; width:200px; background: #ff7a00; color: #0f0f0f; padding: 11px; border: none; cursor: pointer; border-radius: 6px; font-weight: 700; margin: 12px auto 0; text-align: center; }");
  client.println("button:hover, .btn:hover { background: #e56a00; }");
  client.println(".divider { border-top: 1px solid #3a3e41; margin: 18px 0; }");
  client.println(".color-row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }");
  client.println(".color-row input[type='color'] { flex: 1 0 140px; min-height: 44px; padding: 0; border: 1px solid #3a3e41; border-radius: 6px; background: #1b1d1e; }");
  client.println(".color-row .channel-input { flex: 1 0 70px; width: auto; }");
  client.println("</style>");
}


// Helper function to send navigation header
void sendNavigationHeader(const char* pageTitle) {

String topHeader;
  topHeader += "OSC Send: ";
  topHeader += ipToString(netConfig.sendToIP);
  topHeader += ":";
  topHeader += netConfig.sendPort;
  topHeader += " | OSC Receive: ";
  topHeader += ipToString(Ethernet.localIP());
  topHeader += ":";
  topHeader += netConfig.receivePort;
  topHeader += " | Key Send Mode: ";
  topHeader += Fconfig.sendKeystrokes ? "USB" : "OSC";


  client.println("<div class='logo-section'>");
  client.println("<svg class='logo-svg' xmlns='http://www.w3.org/2000/svg' viewBox='0 0 520 320'><text x='215' y='200' text-anchor='end' font-family='DejaVu Sans, Arial, Helvetica, sans-serif' font-weight='700' font-size='110' fill='#ff7a00'>Evo</text><text x='430' y='280' text-anchor='end' font-family='DejaVu Sans, Arial, Helvetica, sans-serif' font-weight='700' font-size='80' fill='#ff7a00'>FaderWing</text><g class='fader-bank' stroke='#000'><circle cx='242' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='230' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='230' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='230' y='160' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='282' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='270' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='270' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='270' y='154' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='322' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='310' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='310' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='310' y='145' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='362' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='350' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='350' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='350' y='168' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/><circle cx='402' cy='85' r='8' stroke-width='3' fill='#ff7a00'/><rect x='390' y='100' width='24' height='16' rx='4' ry='4' fill='#222' stroke-width='2'/><rect x='390' y='125' width='24' height='80' rx='6' ry='6' fill='none' stroke-width='3'/><rect x='390' y='150' width='24' height='30' rx='6' ry='6' fill='#ff7a00' stroke-width='2'/></g></svg>");


  bool isNetwork = strcmp(pageTitle, "Network Settings") == 0 || strcmp(pageTitle, "Network/Debug") == 0;
  bool isOsc = strcmp(pageTitle, "OSC Settings") == 0 || strcmp(pageTitle, "OSC") == 0;
  bool isFader = strcmp(pageTitle, "Fader Configuration") == 0 || strcmp(pageTitle, "Faders") == 0;
  bool isLED = strcmp(pageTitle, "LED Settings") == 0 || strcmp(pageTitle, "LEDs") == 0;
  bool isStats = strcmp(pageTitle, "Statistics") == 0;

  client.println("<div class='nav-links'>");

  client.print("<a href='/'");
  if (isNetwork) client.print(" class='active'");
  client.println(">Network/Debug</a>");

  client.print("<a href='/osc_settings'");
  if (isOsc) client.print(" class='active'");
  client.println(">OSC</a>");

  client.print("<a href='/fader_settings'");
  if (isFader) client.print(" class='active'");
  client.println(">Faders</a>");

  client.print("<a href='/led_settings'");
  if (isLED) client.print(" class='active'");
  client.println(">LEDs</a>");

  client.print("<a href='/stats'");
  if (isStats) client.print(" class='active'");
  client.println(">Statistics</a>");
  client.println("</div>");

  // Ip bar
  client.println("</div><div class='ip-bar'>");
  client.print(topHeader);
  client.println("</div>");


}

void sendFooter() {
  client.println(F("<div class='ip-bar'>V" SW_VERSION " - by Shawn R</div>"));
}

void sendMessagePage(const char* title, const char* message, const char* redirectUrl, int redirectSeconds) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<link rel='icon' type='image/svg+xml' href='/favicon.svg'>");
  if (redirectUrl && redirectUrl[0] != '\0' && redirectSeconds > 0) {
    client.print("<script>setTimeout(function(){ window.location.replace('");
    client.print(redirectUrl);
    client.print("'); }, ");
    client.print(redirectSeconds * 1000);
    client.println(");</script>");
  }
  client.println("<style>");
  client.println("body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif; background: #181a1b; color: #e8e6e3; margin: 0; padding: 20px; }");
  client.println(".msg-container { max-width: 520px; margin: 60px auto; background: #202324; border: 1px solid #2d3133; border-radius: 10px; padding: 24px 20px; box-shadow: 0 2px 6px rgba(0,0,0,0.35); text-align: center; }");
  client.println(".msg-container h1 { margin: 0 0 12px; font-size: 24px; color: #ff7a00; }");
  client.println(".msg-container p { margin: 8px 0; color: #a8a095; line-height: 1.5; }");
  client.println(".msg-container a { color: #ff7a00; text-decoration: none; font-weight: 700; }");
  client.println(".msg-container a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='msg-container'>");
  client.print("<h1>");
  client.print(title);
  client.println("</h1>");
  client.print("<p>");
  client.print(message);
  client.println("</p>");
  if (redirectUrl && redirectUrl[0] != '\0') {
    client.print("<p><a href='");
    client.print(redirectUrl);
    client.println("'>Continue</a></p>");
  }
  client.println("</div></body></html>");
}

void handleStatsData() {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println(F("Connection: close"));
  client.println();

  client.print(F("{\"faders\":["));
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    int oscVal = readFadertoOSC(f);
    int currentVal = (f.lastAnalogValue >= 0) ? f.lastAnalogValue : 0;

    if (i > 0) client.print(',');
    client.print(F("{\"id\":"));
    client.print(i + 1);
    client.print(F(",\"current\":"));
    client.print(currentVal);
    client.print(F(",\"min\":"));
    client.print(f.minVal);
    client.print(F(",\"max\":"));
    client.print(f.maxVal);
    client.print(F(",\"osc\":"));
    client.print(oscVal);
    client.print('}');

    if (i % 3 == 0) waitForWriteSpace(200);
  }
  client.println(F("]}"));
}


void handleStatsPage() {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html><html><head><title>Fader Statistics</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  sendCommonStyles();
  client.println("<style>");
  client.println("table { width: 100%; border-collapse: collapse; }");
  client.println("th, td { border: 1px solid #3a3e41; padding: 8px; text-align: left; }");
  client.println("th { background: #ff7a00; color: #0f0f0f; }");
  client.println("</style>");
  client.println("</head><body>");
  
  sendNavigationHeader("Statistics");
  
  client.println("<div class='container'>");
  client.println("<div class='card'>");
  client.println("<h2>Live Fader Stats</h2>");
  
  client.println("<table id='stats-table'>");
  client.println("<tr><th>Fader</th><th>Current</th><th>Min</th><th>Max</th><th>OSC Value</th></tr>");
  client.println("<tbody id='stats-body'><tr><td colspan='5'>Loading...</td></tr></tbody></table>");

  client.println("</div>");
  client.println("</div>");
  waitForWriteSpace(600);
  client.println(F("<script>"
    "const statsBody=document.getElementById('stats-body');"
    "function renderStats(data){if(!data||!data.faders)return;"
    "let rows='';"
    "for(let i=0;i<data.faders.length;i++){const f=data.faders[i];"
    "rows+=`<tr><td>Fader ${f.id}</td><td>${f.current}</td><td>${f.min}</td><td>${f.max}</td><td>${f.osc}</td></tr>`;}"
    "statsBody.innerHTML=rows;}"
    "async function refreshStats(){try{const res=await fetch('/stats_data');if(!res.ok)return;const data=await res.json();renderStats(data);}catch(e){}}"
    "refreshStats();"
    "setInterval(refreshStats,500);"
    "</script>"));
  sendFooter();
  client.println("</body></html>");
}

void handleFaderSettingsPage() {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close"));
  client.println();

  client.println(F("<!DOCTYPE html><html><head><title>Fader Configuration</title>"));
  client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  client.println(F("</head><body>"));

  sendNavigationHeader("Fader Configuration");

  client.println(F("<div class='container'>"));

  waitForWriteSpace(600);

  client.print(F(
    "<div class='card'><div class='card-header'><h2>Fader Settings</h2></div><div class='card-body'><form method='get' action='/save'>"
    "<div class='form-group'><label>Min Speed</label><input type='number' name='minPwm' value='"));
  client.print(Fconfig.minPwm);
  client.print(F(
    "' min='0' max='255'><p class='help-text'>Too low stalls motor, too high passes setpoint and causes jitter) (0-255)</p></div>"
    "<div class='form-group'><label>Max Speed</label><input type='number' name='maxPwm' value='"));
  client.print(Fconfig.maxPwm);
  client.print(F(
    "' min='0' max='255'><p class='help-text'>Max motor speed (0-255)</p></div>"
    "<div class='form-group'><label>Slow Speed Zone</label><input type='number' name='slowZone' value='"));
  client.print(Fconfig.slowZone);
  client.print(F(
    "' min='0' max='100'><p class='help-text'>Fader runs at min speed when nearer than this distance to the setpoint.</p></div>"
    "<div class='form-group'><label>Fast Speed Zone</label><input type='number' name='fastZone' value='"));
  client.print(Fconfig.fastZone);
  
  waitForWriteSpace(600);

  client.print(F(
    "' min='1' max='100'><p class='help-text'>Fader runs at max speed when farther than this distance from the setpoint.</p></div>"
    "<p class='help-text'>Between these distances, speed scales smoothly from min to max.</p>"
    "<div class='divider'></div>"
    "<div class='form-group'><label>Target Tolerance</label><input type='number' name='targetTolerance' value='"));
  client.print(Fconfig.targetTolerance);
  client.println(F(
    "' min='0' max='100'><p class='help-text'>Position accuracy before motor stops</p></div>"));

  waitForWriteSpace(600);

  client.print(F("<div class='form-group'><label>Send Tolerance</label><input type='number' name='sendTolerance' value='"));
  client.print(Fconfig.sendTolerance);
  client.println(F(
    "' min='0' max='100'><p class='help-text'>Min movement before OSC update <2 can cause jitter</p></div>"
    "<button type='submit' class='btn btn-primary btn-block'>Save Fader Settings</button>"
    "</form></div></div>"));

  waitForWriteSpace(600);

  client.print(F(
    "<div class='card' style='margin-top: 20px;'><div class='card-header'><h2>Calibration & Touch</h2></div><div class='card-body'>"
    "<form method='get' action='/save'><div class='form-group'><label>Motor Calibration Speed</label><input type='number' name='calib_pwm' value='"));
  client.print(Fconfig.calibratePwm);
  client.println(F(
    "' min='0' max='255'><p class='help-text'>Motor speed during calibration (lower = gentler)</p></div><button type='submit' class='btn btn-success btn-block'>Save Calibration Speed</button></form>"
    "<form method='post' action='/calibrate'><input type='hidden' name='calibrate' value='1'><button type='submit' class='btn btn-info btn-block'>Run Fader Calibration</button></form>"
    "<p class='help-text'>Calibration also clears any disabled fader motors.</p>"
    "<div class='divider'></div>"
    "<form method='get' action='/save'><h3 style='margin: 0 0 10px;'>Touch Sensor</h3>"));

#if defined(TOUCH_SENSOR_MTCH2120)
  client.print(F("<div class='form-group'><label>Auto Calibration (AutoTune)</label><select name='autoCalMode'>"
#else
  client.print(F("<div class='form-group'><label>Auto Calibration</label><select name='autoCalMode'>"
#endif
    "<option value='0'"));
  if (autoCalibrationMode == 0) client.print(F(" selected"));
  client.print(F(">Disabled</option><option value='1'"));
  if (autoCalibrationMode == 1) client.print(F(" selected"));
  client.println(F(">Enabled</option></select>"
#if defined(TOUCH_SENSOR_MTCH2120)
    "<p class='help-text'>MTCH2120 AutoTune baseline tracking. Leave enabled unless troubleshooting.</p>"
#else
    "<p class='help-text'>Toggles the built-in autoconfig for baselines. Disabled leaves power-up defaults.</p>"
#endif
    "</div>"));

  client.print(F("<div class='form-group'><label>Touch Threshold</label><input type='number' name='touchThreshold' value='"));
  client.print(touchThreshold);
  client.println(F("' min='1' max='255'><p class='help-text'>Higher values = less sensitive"
#if defined(TOUCH_SENSOR_MTCH2120)
    " (default: 128)"
#else
    " (default: 12)"
#endif
    "</p></div>"));

#if defined(TOUCH_SENSOR_MTCH2120)
  client.print(F("<div class='form-group'><label>Hysteresis</label><input type='number' name='releaseThreshold' value='"));
  client.print(releaseThreshold);
  client.println(F("' min='0' max='7'><p class='help-text'>MTCH2120 HYS code (0-7). 1 = ~25% (default), higher = stickier releases.</p></div>"));
#else
  client.print(F("<div class='form-group'><label>Release Threshold</label><input type='number' name='releaseThreshold' value='"));
  client.print(releaseThreshold);
  client.println(F("' min='1' max='255'><p class='help-text'>Lower values = harder to release (default: 6)</p></div>"));
#endif

  client.println(F(
    "<button type='submit' class='btn btn-primary btn-block'>Save Touch Settings</button>"
    "<p class='help-text' style='margin-top: 12px; color: red;'>Do not touch faders while saving</p>"
    "</form></div></div>"));

  waitForWriteSpace(800);
  client.println(F("</div>")); // container
  sendFooter();
  client.println(F("</body></html>"));
}

void handleLEDSettingsPage() {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close"));
  client.println();
  
  client.println(F("<!DOCTYPE html><html><head><title>Fader LEDs</title>"));
  client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1'>"));
  sendCommonStyles();
  client.println(F("</head><body>"));
  
  sendNavigationHeader("LED Settings");
  
  client.println(F("<div class='container'>"));
  waitForWriteSpace(600);
  
  client.print(F("<div class='card'><div class='card-header'><h2>Fader LEDs</h2></div><div class='card-body'><form method='get' action='/save'>"
                 "<div class='form-group'><label>Base Level</label><input type='number' name='bb' value='"));
  client.print(Fconfig.baseBrightness);
  client.print(F("' min='0' max='255'><p class='help-text'>LED brightness when fader is not touched (0-255)</p></div>"
                 "<div class='form-group'><label>Touched Level</label><input type='number' name='tb' value='"));
  client.print(Fconfig.touchedBrightness);
  client.print(F("' min='0' max='255'><p class='help-text'>LED brightness when fader is touched (0-255)</p></div>"
                 "<div class='form-group'><label>Fade Time</label><input type='number' name='ft' value='"));
  client.print(Fconfig.fadeTime);
  client.println(F("' min='0' max='10000'><p class='help-text'>Time in ms that the LEDs will fade</p></div>"
                   "<div class='form-group'><label>LED Mode</label><label style='display: inline-block; margin-top: 6px;'>"
                   "<input type='checkbox' name='lp' value='on'"));
  if (Fconfig.useLevelPixels) client.print(F(" checked"));
  
  waitForWriteSpace(800);

  client.print(F("> Show level bars instead of full fill</label><p class='help-text'>When enabled the fader lights up to match the position.</p></div>"
                 "<div class='divider'></div>"
                 "<h3 style='margin: 6px 0;'>Exec LEDs</h3>"
                 "<div class='form-group'><label>Off Level</label><input type='number' name='eb' value='"));
  client.print((int)execConfig.baseBrightness);
  client.print(F("' min='0' max='255'><p class='help-text'>Level when populated/off.</p></div>"
                 "<div class='form-group'><label>On Level</label><input type='number' name='ea' value='"));
  client.print((int)execConfig.activeBrightness);
  client.print(F("' min='0' max='255'><p class='help-text'>Level when active/on.</p></div>"
                 "<div class='form-group'><label><input type='checkbox' name='sc' value='on'"));
  if (execConfig.useStaticColor) client.print(F(" checked"));
  client.print(F("> Use Static Color</label><p class='help-text'>Static color overrides appearance.</p></div>"
                 "<div class='form-group'><label>Static Color</label><div class='color-row'>"
                 "<input type='color' id='staticColorPicker' name='sch' value='"));

  waitForWriteSpace(600);

  char staticHex[8];
  snprintf(staticHex, sizeof(staticHex), "#%02X%02X%02X", execConfig.staticRed, execConfig.staticGreen, execConfig.staticBlue);
  client.print(staticHex);
  client.print(F("'><input type='number' class='channel-input' id='staticRed' name='sr' min='0' max='255' value='"));
  client.print((int)execConfig.staticRed);
  client.print(F("'><input type='number' class='channel-input' id='staticGreen' name='sg' min='0' max='255' value='"));
  client.print((int)execConfig.staticGreen);
  client.print(F("'><input type='number' class='channel-input' id='staticBlue' name='sb' min='0' max='255' value='"));
  client.print((int)execConfig.staticBlue);
  client.println(F("'></div><p class='help-text'>Static picker or RGB values (0-255).</p></div>"
                   "<button type='submit' class='btn btn-primary btn-block'>Save LED Settings</button>"
                   "</form></div></div>"));
  
  waitForWriteSpace(600);
  client.println(F("</div>")); // container

  client.println(F("<script>"
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
  client.println(F("</body></html>"));
}

//================================
// MAIN WEB PAGE HANDLER (ROOT - Network Settings)
//================================
void handleRoot() {
  // Send HTTP headers
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Send HTML head
  client.println("<!DOCTYPE html><html><head><title>Network Settings</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  sendCommonStyles();
  client.println("</head><body>");
  
  // Send navigation header
  sendNavigationHeader("Network Settings");
  
  client.println(F("<div class='container'>"));
  waitForWriteSpace(400);

  // Network Settings Card
  client.println(F("<div class='card'><h2>Network Settings</h2><form method='get' action='/save'>"));
  client.print(F("<label><input type='checkbox' name='dhcp' value='on'"));
  if (netConfig.useDHCP) client.print(F(" checked"));
  client.println(F("> Use DHCP</label><p class='help'>When enabled, static IP settings below are ignored</p>"));

  client.print(F("<label>Static IP Address</label><input type='text' name='ip' value='"));
  client.print(ipToString(netConfig.staticIP));
  client.println(F("'>"));

  client.print(F("<label>Gateway</label><input type='text' name='gw' value='"));
  client.print(ipToString(netConfig.gateway));
  client.println(F("'>"));

  client.print(F("<label>Subnet Mask</label><input type='text' name='sn' value='"));
  client.print(ipToString(netConfig.subnet));
  client.println(F("'>"));

  client.println(F("<button type='submit'>Save Network Settings</button></form>"));
  client.println(F("<form method='post' action='/reset_network'>"
                   "<button type='submit' onclick=\"return confirm('Reset network settings?');\">Reset Network</button>"
                   "</form></div>"));

  waitForWriteSpace(400);

  // Debug Tools Card
  client.println(F("<div class='card'><h2>Debug Tools</h2>"));
  client.println(F("<form method='post' action='/debug'><input type='hidden' name='debug' value='0'><label>"));
  client.print(F("<input type='checkbox' name='debug' value='1'"));
  if (debugMode) client.print(F(" checked"));
  client.println(F("> Enable Serial Debug Output</label><p class='help-text' style='color: red;'>Serial debug can cause jitter and slows all processes, DO NOT run in production</p><button type='submit'>Save Debug Setting</button></form>"));

  client.println(F("<div class='divider'></div><form method='post' action='/dump'>"
                   "<button type='submit'>Dump EEPROM to Serial</button>"
                   "</form></div>"));

  waitForWriteSpace(400);

  // Factory Reset Teensy Reboot Card
  client.println(F("<div class='card'><h2>Factory Reset</h2>"
                   "<p>This will reset all settings to factory defaults.</p>"
                   "<form method='post' action='/reset_defaults'>"
                   "<button type='submit' onclick=\"return confirm('Reset ALL settings?');\">Reset All Settings</button>"
                   "</form>"
                   "<form method='post' action='/reboot'>"
                   "<button type='submit' onclick=\"return confirm('Reboot EvoFaderWing?');\">Reboot</button>"
                   "</form>"
                   "</div>"));

  client.println(F("</div>")); // End container
  sendFooter();
  client.println(F("</body></html>"));
}

void waitForWriteSpace(size_t minBytes) {
  while (client.connected()) {
    int available = client.availableForWrite();
    if (available < 0 || static_cast<size_t>(available) >= minBytes) {
      break;
    }
    Ethernet.loop();
    delay(1);
  }
}



void handleGMA3ShortcutsDownload() {
  debugPrint("Serving GMA3 shortcuts XML file download...");
  
  // Your GMA3 shortcuts XML content
  const char* xmlContent = R"(<?xml version="1.0" encoding="UTF-8"?>
<GMA3 DataVersion="1.9.3.3">
    <KeyboardShortCuts KeyboardShortcutsActive="Yes">
        <!-- Row 1 (101-110) -->
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="101" Shortcut="Z" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="102" Shortcut="X" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="103" Shortcut="C" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="104" Shortcut="V" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="105" Shortcut="B" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="106" Shortcut="N" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="107" Shortcut="M" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="108" Shortcut="Comma" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="109" Shortcut="Period" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="110" Shortcut="Slash" />
        
        <!-- Row 2 (201-210) -->
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="201" Shortcut="A" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="202" Shortcut="S" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="203" Shortcut="D" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="204" Shortcut="F" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="205" Shortcut="G" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="206" Shortcut="H" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="207" Shortcut="J" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="208" Shortcut="K" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="209" Shortcut="L" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="210" Shortcut="Semicolon" />
        
        <!-- Row 3 (301-310) -->
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="301" Shortcut="Q" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="302" Shortcut="W" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="303" Shortcut="E" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="304" Shortcut="R" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="305" Shortcut="Equal" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="306" Shortcut="Y" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="307" Shortcut="U" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="308" Shortcut="I" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="309" Shortcut="O" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="310" Shortcut="P" />
        
        <!-- Row 4 (401-410) -->
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="401" Shortcut="Apostrophe" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="402" Shortcut="Space" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="403" Shortcut="Tab" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="404" Shortcut="GraveAccent" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="405" Shortcut="Left" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="406" Shortcut="Right" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="407" Shortcut="Up" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="408" Shortcut="Down" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="409" Shortcut="Backslash" />
        <KeyboardShortcut Lock="Yes" KeyCode="EXEC" ExecutorIndex="410" Shortcut="CapsLock" />
    </KeyboardShortCuts>
</GMA3>)";

  // Send headers for file download
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/xml");
  client.println("Content-Disposition: attachment; filename=\"EvoFaderWing_keyboard_shortcuts.xml\"");
  client.print("Content-Length: ");
  client.println(strlen(xmlContent));
  client.println("Connection: close");
  client.println();
  
  // Send the XML content
  client.print(xmlContent);
}
