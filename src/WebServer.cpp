// WebServer.cpp - Memory Optimized Version

#include "WebServer.h"
#include "Utils.h"
#include "EEPROMStorage.h"
#include "FaderControl.h"
#include "TouchSensor.h"
#include <QNEthernet.h>

using namespace qindesign::network;

void waitForWriteSpace();

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

void sendErrorResponse(const char* errorMsg) {
  client.println("HTTP/1.1 400 Bad Request");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }");
  client.println(".error-container { background: white; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 500px; margin: 50px auto; }");
  client.println("h1 { color: #d32f2f; margin-top: 0; }");
  client.println("p { color: #666; line-height: 1.6; }");
  client.println("a { color: #1976d2; text-decoration: none; font-weight: 500; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='error-container'>");
  client.println("<h1>Error</h1>");
  client.printf("<p>%s</p>", errorMsg);
  client.println("<p><a href='/'>← Return to settings</a></p>");
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
        } else if (request.indexOf("pidKp=") >= 0) {
          requestType = 'P'; // PID settings
          debugPrint("Determined: PID settings");
        } else if (request.indexOf("touchThreshold=") >= 0) {
          requestType = 'T'; // Touch settings
          debugPrint("Determined: Touch settings");
        } else if (request.indexOf("motorDeadzone=") >= 0 || request.indexOf("baseBrightness=") >= 0) {
          requestType = 'F'; // Fader settings (including brightness)
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
      } else if (path == "/reset_network" && method == "POST") {
        requestType = 'Z'; // Reset network settings
      } else if (path == "/stats") {  
        requestType = 'S'; // Stats page
      } else if (path == "/fader_settings") {
        requestType = 'G'; // Fader settings page
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

        case 'T': // Touch settings
          handleTouchSettings(request);
          break;

        case 'X': // Reset to defaults
          handleResetDefaults();
          break;

        case 'Z': // Reset network settings
          handleNetworkReset();
          break;
        case 'S': // Stats page
            handleStatsPage();
            break;
        case 'G': // Fader settings page
            handleFaderSettingsPage();
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
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }");
  client.println(".error-container { background: white; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 500px; margin: 50px auto; text-align: center; }");
  client.println("h1 { color: #d32f2f; margin-top: 0; font-size: 72px; margin-bottom: 10px; }");
  client.println("h2 { color: #333; margin-top: 0; }");
  client.println("p { color: #666; line-height: 1.6; }");
  client.println("a { color: #1976d2; text-decoration: none; font-weight: 500; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='error-container'>");
  client.println("<h1>404</h1>");
  client.println("<h2>Page Not Found</h2>");
  client.println("<p>The requested resource was not found on this server.</p>");
  client.println("<p><a href='/'>← Return to home</a></p>");
  client.println("</div></body></html>");
}

void handleDebugToggle(String requestBody) {
  debugPrint("Processing debug mode toggle...");
  
  debugPrintf("Debug toggle request body: '%s'\n", requestBody.c_str());
  
  // Check for debug=1 in the request body
  // Initially set to false (default from hidden field value=0)
  debugMode = false;
  
  // Then check if the checkbox value (debug=1) is present
  if (requestBody.indexOf("debug=1") != -1) {
    debugMode = true;
  }
  
  debugPrintf("Debug mode set to: %d\n", debugMode ? 1 : 0);
  
  // Redirect back to main page
  sendRedirect();
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
  
  // Send success response with improved styling
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }");
  client.println(".success-container { background: white; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 500px; margin: 50px auto; }");
  client.println("h1 { color: #2e7d32; margin-top: 0; }");
  client.println("p { color: #666; line-height: 1.6; }");
  client.println("a { color: #1976d2; text-decoration: none; font-weight: 500; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='success-container'>");
  client.println("<h1>Network Settings Saved</h1>");
  client.println("<p>Network settings have been saved successfully. For changes to take full effect, please restart the device.</p>");
  client.println("<p><a href='/'>← Return to settings</a></p>");
  client.println("</div></body></html>");
}

void handleOSCSettings(String request) {
  debugPrint("Handling OSC settings only...");
  
  // Extract OSC parameters
  String sendIPStr = getParam(request, "osc_sendip");
  String sendPortStr = getParam(request, "osc_sendport");
  String receivePortStr = getParam(request, "osc_receiveport");
  
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
      debugPrintf("Updated OSC Receive Port: %d\n", netConfig.receivePort);
    } else {
      debugPrintf("ERROR: Invalid OSC receive port: %d\n", newReceivePort);
      sendErrorResponse("Invalid OSC receive port (must be 1-65535)");
      return;
    }
  }
  
  // Save to EEPROM
  saveNetworkConfig();
  
  debugPrint("OSC settings saved successfully");
  sendRedirect();
}

void handleCalibrationSettings(String request) {
  debugPrint("Handling calibration settings...");
  
  // Extract and parse calibration parameters
  Fconfig.calibratePwm = getParam(request, "calib_pwm").toInt();
  debugPrintf("Calibration PWM saved: %d\n", Fconfig.calibratePwm);
  
  // Save to EEPROM
  saveFaderConfig();
  
  // Redirect back to main page
  sendRedirect();
}

void handleFaderSettings(String request) {
  debugPrint("Handling fader settings...");
  
  // Extract and parse fader parameters
  Fconfig.motorDeadzone = getParam(request, "motorDeadzone").toInt();
  Fconfig.defaultPwm = getParam(request, "defaultPwm").toInt();
  Fconfig.targetTolerance = getParam(request, "targetTolerance").toInt();
  Fconfig.sendTolerance = getParam(request, "sendTolerance").toInt();
  
  // NEW: Extract brightness parameters
  String baseBrightnessStr = getParam(request, "baseBrightness");
  String touchedBrightnessStr = getParam(request, "touchedBrightness");
  
  if (baseBrightnessStr.length() > 0) {
    Fconfig.baseBrightness = baseBrightnessStr.toInt();
    debugPrintf("Base Brightness saved: %d\n", Fconfig.baseBrightness);
  }
  
  if (touchedBrightnessStr.length() > 0) {
    Fconfig.touchedBrightness = touchedBrightnessStr.toInt();
    debugPrintf("Touched Brightness saved: %d\n", Fconfig.touchedBrightness);
  }
  
  // Save to EEPROM
  saveFaderConfig();
  
  // Redirect back to main page
  sendRedirect();
}

void handleRunCalibration() {
  debugPrint("Running fader calibration...");
  
  // Run the calibration process
  calibrateFaders();
  saveCalibration();

  // Reinitialize MPR121 after calibration due to I2C hang risk
  debugPrint("Reinitializing touch sensor after calibration...");
  setupTouch();  // Restore I2C communication and config
  
  // Redirect back to main page
  sendRedirect();
}

void handleTouchSettings(String request) {
  debugPrint("Handling touch sensor settings...");
  
  // Extract and parse touch parameters
  autoCalibrationMode = getParam(request, "autoCalMode").toInt();
  touchThreshold = getParam(request, "touchThreshold").toInt();
  releaseThreshold = getParam(request, "releaseThreshold").toInt();
  
  // Apply the settings to the touch sensor
  setAutoTouchCalibration(autoCalibrationMode);
  manualTouchCalibration();
  
  // Save to EEPROM
  saveTouchConfig();
  
  // Reset MPR121
  setupTouch();
  
  // Redirect back to main page
  sendRedirect();
}

void handleResetDefaults() {
  debugPrint("Resetting all settings to defaults...");
  resetToDefaults();
  sendRedirect();
}

void handleNetworkReset() {
  debugPrint("Resetting network settings to defaults...");
  resetNetworkDefaults();
  
  // Special response for network settings with improved styling
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }");
  client.println(".success-container { background: white; border-radius: 8px; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 500px; margin: 50px auto; }");
  client.println("h1 { color: #f57c00; margin-top: 0; }");
  client.println("p { color: #666; line-height: 1.6; }");
  client.println("a { color: #1976d2; text-decoration: none; font-weight: 500; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");
  client.println("<div class='success-container'>");
  client.println("<h1>Network Settings Reset</h1>");
  client.println("<p>Network settings have been reset to defaults. For changes to take full effect, please restart the device.</p>");
  client.println("<p><a href='/'>← Return to settings</a></p>");
  client.println("</div></body></html>");
}

void sendRedirect() {
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}

// Helper function to send CSS styles
void sendCommonStyles() {
  client.println("<style>");
  client.println(":root { --primary: #1976d2; --success: #2e7d32; --warning: #f57c00; --danger: #d32f2f; --bg: #f5f5f5; --card-bg: white; --text: #333; --text-secondary: #666; --border: #e0e0e0; }");
  client.println("* { box-sizing: border-box; }");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 0; background: var(--bg); color: var(--text); line-height: 1.6; }");
  
  // Header styling
  client.println(".header { background: var(--primary); color: white; padding: 20px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
  client.println(".header-content { max-width: 1200px; margin: 0 auto; padding: 0 20px; }");
  client.println(".header h1 { margin: 0; font-weight: 300; font-size: 28px; }");
  client.println(".header p { margin: 5px 0 0 0; opacity: 0.9; font-size: 14px; }");
  
  // Layout
  client.println(".container { max-width: 1200px; margin: 0 auto; padding: 20px; }");
  client.println(".grid { display: grid; grid-template-columns: 1fr 380px; gap: 20px; }");
  client.println("@media (max-width: 768px) { .grid { grid-template-columns: 1fr; } }");
  
  // Card styling
  client.println(".card { background: var(--card-bg); border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); overflow: hidden; }");
  client.println(".card-header { background: #fafafa; padding: 16px 20px; border-bottom: 1px solid var(--border); }");
  client.println(".card-header h2 { margin: 0; font-size: 18px; font-weight: 600; color: var(--text); }");
  client.println(".card-body { padding: 20px; }");
  
  // Form styling
  client.println("form { margin: 0; }");
  client.println(".form-group { margin-bottom: 16px; }");
  client.println(".form-group:last-child { margin-bottom: 0; }");
  client.println("label { display: block; font-weight: 500; margin-bottom: 6px; color: var(--text); }");
  client.println("input[type='text'], input[type='number'], select { width: 100%; padding: 8px 12px; border: 1px solid var(--border); border-radius: 4px; font-size: 14px; transition: border-color 0.2s; }");
  client.println("input[type='text']:focus, input[type='number']:focus, select:focus { outline: none; border-color: var(--primary); }");
  client.println("input[type='checkbox'] { margin-right: 8px; width: 16px; height: 16px; vertical-align: middle; }");
  client.println(".help-text { font-size: 13px; color: var(--text-secondary); margin-top: 4px; }");
  
  // Button styling
  client.println(".btn { padding: 10px 20px; border: none; border-radius: 4px; font-size: 14px; font-weight: 500; cursor: pointer; transition: all 0.2s; text-decoration: none; display: inline-block; }");
  client.println(".btn-primary { background: var(--primary); color: white; }");
  client.println(".btn-primary:hover { background: #1565c0; box-shadow: 0 2px 8px rgba(25,118,210,0.3); }");
  client.println(".btn-success { background: var(--success); color: white; }");
  client.println(".btn-success:hover { background: #256b28; box-shadow: 0 2px 8px rgba(46,125,50,0.3); }");
  client.println(".btn-warning { background: var(--warning); color: white; }");
  client.println(".btn-warning:hover { background: #e65100; box-shadow: 0 2px 8px rgba(245,124,0,0.3); }");
  client.println(".btn-danger { background: var(--danger); color: white; }");
  client.println(".btn-danger:hover { background: #b71c1c; box-shadow: 0 2px 8px rgba(211,47,47,0.3); }");
  client.println(".btn-info { background: #0288d1; color: white; }");
  client.println(".btn-info:hover { background: #0277bd; box-shadow: 0 2px 8px rgba(2,136,209,0.3); }");
  client.println(".btn-block { width: 100%; }");
  client.println(".btn-group { display: flex; gap: 10px; margin-top: 16px; }");
  
  // Status panel styling
  client.println(".status-item { padding: 12px 16px; background: #f9f9f9; border-radius: 4px; margin-bottom: 12px; display: flex; justify-content: space-between; align-items: center; }");
  client.println(".status-label { font-weight: 500; color: var(--text); }");
  client.println(".status-value { color: var(--text-secondary); font-family: 'Courier New', monospace; }");
  client.println(".status-link { margin-top: 16px; }");
  
  // Divider
  client.println(".divider { height: 1px; background: var(--border); margin: 24px 0; }");
  
  // Checkbox container
  client.println(".checkbox-group { display: flex; align-items: center; }");
  
  client.println("</style>");
}

// Helper function to send navigation header
void sendNavigationHeader(const char* pageTitle) {
  client.println("<div class='header'>");
  client.println("<div class='header-content'>");
  client.println("<h1>GMA3 FaderWing Configuration</h1>");
  client.print("<p>Current IP: ");
  client.print(ipToString(Ethernet.localIP()));
  client.println("</p>");
  client.println("<div style='margin-top: 15px;'>");
  client.println("<a href='/' class='btn' style='background: white; color: #1976d2; margin-right: 10px;'>Network Settings</a>");
  client.println("<a href='/fader_settings' class='btn' style='background: white; color: #1976d2; margin-right: 10px;'>Fader Settings</a>");
  client.println("<a href='/stats' class='btn' style='background: white; color: #1976d2;'>Statistics</a>");
  client.println("</div>");
  client.println("</div></div>");
}

void handleStatsPage() {
  // Send HTTP headers
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Send HTML head
  client.println("<!DOCTYPE html><html><head><title>Fader Statistics</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  
  // Send specific styles for stats page
  client.println("<style>");
  client.println("body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 0; background: #f5f5f5; }");
  client.println(".header { background: #1976d2; color: white; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
  client.println(".header h1 { margin: 0; font-weight: 300; }");
  client.println(".container { padding: 20px; max-width: 1200px; margin: 0 auto; }");
  client.println(".stats-card { background: white; border-radius: 8px; padding: 20px; margin-bottom: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
  client.println("table { width: 100%; border-collapse: collapse; }");
  client.println("th { background: #f5f5f5; text-align: left; padding: 12px; font-weight: 600; color: #666; border-bottom: 2px solid #e0e0e0; }");
  client.println("td { padding: 12px; border-bottom: 1px solid #e0e0e0; }");
  client.println("tr:hover { background: #f9f9f9; }");
  client.println(".back-link { display: inline-block; color: white; text-decoration: none; margin-bottom: 10px; opacity: 0.8; }");
  client.println(".back-link:hover { opacity: 1; }");
  client.println(".status-active { color: #2e7d32; font-weight: 600; }");
  client.println(".status-idle { color: #666; }");
  client.println(".range-bar { height: 20px; background: #e0e0e0; border-radius: 10px; position: relative; overflow: hidden; }");
  client.println(".range-fill { height: 100%; background: #1976d2; transition: width 0.3s; }");
  client.println(".btn { padding: 10px 20px; border: none; border-radius: 4px; font-size: 14px; font-weight: 500; cursor: pointer; transition: all 0.2s; text-decoration: none; display: inline-block; }");
  client.println("</style></head><body>");
  
  // Send navigation header
  sendNavigationHeader("Fader Statistics");
  
  // Send main content
  client.println("<div class='container'>");
  client.println("<div class='stats-card'>");
  client.println("<table>");
  client.println("<tr><th>Fader</th><th>Current Value</th><th>Min</th><th>Max</th><th>Range</th><th>Visual Range</th><th>OSC Value</th></tr>");
  
  // Send fader data
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    int currentVal = analogRead(f.analogPin);
    int range = f.maxVal - f.minVal;
    float percentage = (range > 0) ? ((float)(currentVal - f.minVal) / range * 100) : 0;
    
    client.print("<tr>");
    client.print("<td><strong>Fader ");
    client.print(i + 1);
    client.println("</strong></td>");
    
    client.print("<td>");
    client.print(currentVal);
    client.println("</td>");
    
    client.print("<td>");
    client.print(f.minVal);
    client.println("</td>");
    
    client.print("<td>");
    client.print(f.maxVal);
    client.println("</td>");
    
    client.print("<td>");
    client.print(range);
    client.println("</td>");
    
    client.print("<td><div class='range-bar'><div class='range-fill' style='width: ");
    client.print(percentage);
    client.println("%;'></div></div></td>");
    
    client.print("<td>");
    client.print(readFadertoOSC(faders[i]));
    client.println("</td>");
    
    client.println("</tr>");
  }
  
  client.println("</table>");
  client.println("</div></div></body></html>");
}

void handleFaderSettingsPage() {
  // Send HTTP headers
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Send HTML head
  client.println("<!DOCTYPE html><html><head><title>Fader Configuration</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  
  // Send common styles
  sendCommonStyles();
  client.println("</head><body>");
  
  // Send navigation header
  sendNavigationHeader("Fader Configuration");
  
  // Main container
  client.println("<div class='container'>");
  
waitForWriteSpace();

  // Fader Settings Card
  client.println("<div class='card'>");
  client.println("<div class='card-header'><h2>Fader Settings</h2></div>");
  client.println("<div class='card-body'>");
  client.println("<form method='get' action='/save'>");
  
  // Motor Deadzone
  client.println("<div class='form-group'>");
  client.println("<label>Motor Deadzone</label>");
  client.print("<input type='number' name='motorDeadzone' value='");
  client.print(Fconfig.motorDeadzone);
  client.println("' min='0' max='100'>");
  client.println("<p class='help-text'>Minimum error before motor stops (prevents jitter)</p>");
  client.println("</div>");
  
  // Default PWM
  client.println("<div class='form-group'>");
  client.println("<label>Default PWM Speed</label>");
  client.print("<input type='number' name='defaultPwm' value='");
  client.print(Fconfig.defaultPwm);
  client.println("' min='0' max='255'>");
  client.println("<p class='help-text'>Base motor speed (0-255)</p>");
  client.println("</div>");
  
  // Target Tolerance
  client.println("<div class='form-group'>");
  client.println("<label>Target Tolerance</label>");
  client.print("<input type='number' name='targetTolerance' value='");
  client.print(Fconfig.targetTolerance);
  client.println("' min='0' max='100'>");
  client.println("<p class='help-text'>Position accuracy before motor stops</p>");
  client.println("</div>");
  
waitForWriteSpace();

  // Send Tolerance
  client.println("<div class='form-group'>");
  client.println("<label>Send Tolerance</label>");
  client.print("<input type='number' name='sendTolerance' value='");
  client.print(Fconfig.sendTolerance);
  client.println("' min='0' max='100'>");
  client.println("<p class='help-text'>Minimum movement before sending OSC update</p>");
  client.println("</div>");
  
  // Brightness controls
  client.println("<div class='divider'></div>");
  client.println("<h3 style='margin-top: 0; margin-bottom: 16px; font-size: 16px;'>LED Brightness</h3>");
  
  client.println("<div class='form-group'>");
  client.println("<label>Base Brightness</label>");
  client.print("<input type='number' name='baseBrightness' value='");
  client.print(Fconfig.baseBrightness);
  client.println("' min='0' max='255'>");
  client.println("<p class='help-text'>LED brightness when fader is not touched (0-255)</p>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>Touched Brightness</label>");
  client.print("<input type='number' name='touchedBrightness' value='");
  client.print(Fconfig.touchedBrightness);
  client.println("' min='0' max='255'>");
  client.println("<p class='help-text'>LED brightness when fader is touched (0-255)</p>");
  client.println("</div>");
  
  client.println("<button type='submit' class='btn btn-primary btn-block'>Save Fader Settings</button>");
  client.println("</form></div></div>");
  

  waitForWriteSpace();


  // Calibration Card
  client.println("<div class='card' style='margin-top: 20px;'>");
  client.println("<div class='card-header'><h2>Calibration</h2></div>");
  client.println("<div class='card-body'>");
  
  client.println("<form method='get' action='/save'>");
  client.println("<div class='form-group'>");
  client.println("<label>Calibration PWM Speed</label>");
  client.print("<input type='number' name='calib_pwm' value='");
  client.print(Fconfig.calibratePwm);
  client.println("' min='0' max='255'>");
  client.println("<p class='help-text'>Motor speed during calibration (lower = gentler)</p>");
  client.println("</div>");
  client.println("<button type='submit' class='btn btn-success btn-block'>Save Calibration Speed</button>");
  client.println("</form>");
  
  client.println("<div class='divider'></div>");
  
  client.println("<form method='post' action='/calibrate'>");
  client.println("<input type='hidden' name='calibrate' value='1'>");
  client.println("<button type='submit' class='btn btn-info btn-block'>Run Fader Calibration</button>");
  client.println("</form>");
  client.println("</div></div>");
  
  // Touch Sensor Card
  client.println("<div class='card' style='margin-top: 20px;'>");
  client.println("<div class='card-header'><h2>Touch Sensor</h2></div>");
  client.println("<div class='card-body'>");
  client.println("<form method='get' action='/save'>");
  
waitForWriteSpace();

  client.println("<div class='form-group'>");
  client.println("<label>Auto Calibration Mode</label>");
  client.println("<select name='autoCalMode'>");
  client.print("<option value='0'");
  if (autoCalibrationMode == 0) client.print(" selected");
  client.println(">Disabled</option>");
  client.print("<option value='1'");
  if (autoCalibrationMode == 1) client.print(" selected");
  client.println(">Normal (Recommended)</option>");
  client.print("<option value='2'");
  if (autoCalibrationMode == 2) client.print(" selected");
  client.println(">Conservative</option>");
  client.println("</select>");
  client.println("<p class='help-text'>Automatic baseline adjustment for environmental changes</p>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>Touch Threshold</label>");
  client.print("<input type='number' name='touchThreshold' value='");
  client.print(touchThreshold);
  client.println("' min='1' max='255'>");
  client.println("<p class='help-text'>Higher values = less sensitive (default: 12)</p>");
  client.println("</div>");
  
waitForWriteSpace();

  client.println("<div class='form-group'>");
  client.println("<label>Release Threshold</label>");
  client.print("<input type='number' name='releaseThreshold' value='");
  client.print(releaseThreshold);
  client.println("' min='1' max='255'>");
  client.println("<p class='help-text'>Lower values = harder to release (default: 6)</p>");
  client.println("</div>");
  
  client.println("<button type='submit' class='btn btn-primary btn-block'>Save Touch Settings</button>");
  client.println("<p class='help-text' style='margin-top: 12px; color: var(--warning);'>⚠️ Do not touch faders while saving</p>");
  client.println("</form></div></div>");
  
  client.println("</div>"); // End container
  client.println("</body></html>");
}

//================================
// MAIN WEB PAGE HANDLER
//================================

void handleRoot() {
  // Send HTTP headers
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Send HTML head
  client.println("<!DOCTYPE html><html><head><title>Fader Configuration</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  
  // Send common styles
  sendCommonStyles();
  client.println("</head><body>");
  
  // Send navigation header
  sendNavigationHeader("Network Settings");
  
  // Main container
  client.println("<div class='container'>");
  client.println("<div class='grid'>");
  
  // Left column - Settings
  client.println("<div class='left-column'>");
  
  // Network Settings Card
  client.println("<div class='card'>");
  client.println("<div class='card-header'><h2>Network Settings</h2></div>");
  client.println("<div class='card-body'>");
  client.println("<form method='get' action='/save'>");
  
  client.println("<div class='form-group'>");
  client.println("<div class='checkbox-group'>");
  client.print("<label><input type='checkbox' name='dhcp' value='on'");
  if (netConfig.useDHCP) client.print(" checked");
  client.println(">Use DHCP</label>");
  client.println("</div>");
  client.println("<p class='help-text'>When enabled, static IP settings below are ignored</p>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>Static IP Address</label>");
  client.print("<input type='text' name='ip' value='");
  client.print(ipToString(netConfig.staticIP));
  client.println("' placeholder='192.168.1.100'>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>Gateway</label>");
  client.print("<input type='text' name='gw' value='");
  client.print(ipToString(netConfig.gateway));
  client.println("' placeholder='192.168.1.1'>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>Subnet Mask</label>");
  client.print("<input type='text' name='sn' value='");
  client.print(ipToString(netConfig.subnet));
  client.println("' placeholder='255.255.255.0'>");
  client.println("</div>");
  
waitForWriteSpace();

  client.println("<div class='btn-group'>");
  client.println("<button type='submit' class='btn btn-primary'>Save Network Settings</button>");
  client.println("</form>");
  client.println("<form method='post' action='/reset_network' style='margin:0;'>");
  client.println("<button type='submit' class='btn btn-warning' onclick=\"return confirm('Reset network settings to defaults?');\">Reset Network</button>");
  client.println("</form>");
  client.println("</div></div></div>");
  
  // OSC Settings Card
  client.println("<div class='card' style='margin-top: 20px;'>");
  client.println("<div class='card-header'><h2>OSC Communication</h2></div>");
  client.println("<div class='card-body'>");
  client.println("<form method='get' action='/save'>");
  
  client.println("<div class='form-group'>");
  client.println("<label>OSC Send IP</label>");
  client.print("<input type='text' name='osc_sendip' value='");
  client.print(ipToString(netConfig.sendToIP));
  client.println("' placeholder='192.168.1.50'>");
  client.println("<p class='help-text'>IP address of your DAW/software to receive OSC messages</p>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>OSC Send Port</label>");
  client.print("<input type='number' name='osc_sendport' value='");
  client.print(netConfig.sendPort);
  client.println("' min='1' max='65535' placeholder='9000'>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label>OSC Receive Port</label>");
  client.print("<input type='number' name='osc_receiveport' value='");
  client.print(netConfig.receivePort);
  client.println("' min='1' max='65535' placeholder='8000'>");
  client.println("</div>");
  
waitForWriteSpace();

  client.println("<button type='submit' class='btn btn-primary btn-block'>Save OSC Settings</button>");
  client.println("</form></div></div>");
  
  // Navigation Card for Fader Settings
  client.println("<div class='card' style='margin-top: 20px;'>");
  client.println("<div class='card-header'><h2>Fader Configuration</h2></div>");
  client.println("<div class='card-body'>");
  client.println("<p style='margin-bottom: 16px;'>Configure motor settings, LED brightness, calibration, and touch sensor parameters.</p>");
  client.println("<a href='/fader_settings' class='btn btn-primary btn-block'>Go to Fader Settings →</a>");
  client.println("</div></div>");
  
  client.println("</div>"); // End left column
  
  // Right column - Status and Tools
  client.println("<div class='right-column'>");
  
  // Status Card
  client.println("<div class='card'>");
  client.println("<div class='card-header'><h2>System Status</h2></div>");
  client.println("<div class='card-body'>");
  
  client.println("<div class='status-item'>");
  client.println("<span class='status-label'>Current IP</span>");
  client.print("<span class='status-value'>");
  client.print(ipToString(Ethernet.localIP()));
  client.println("</span>");
  client.println("</div>");
  
  client.println("<div class='status-item'>");
  client.println("<span class='status-label'>DHCP Status</span>");
  client.print("<span class='status-value'>");
  client.print(netConfig.useDHCP ? "Enabled" : "Disabled");
  client.println("</span>");
  client.println("</div>");
  
  client.println("<div class='status-item'>");
  client.println("<span class='status-label'>OSC Target</span>");
  client.print("<span class='status-value'>");
  client.print(ipToString(netConfig.sendToIP));
  client.print(":");
  client.print(netConfig.sendPort);
  client.println("</span>");
  client.println("</div>");
  
waitForWriteSpace();

  client.println("<div class='status-item'>");
  client.println("<span class='status-label'>Debug Mode</span>");
  client.print("<span class='status-value'>");
  client.print(debugMode ? "Active" : "Inactive");
  client.println("</span>");
  client.println("</div>");
  
  client.println("<div class='status-link'>");
  client.println("<a href='/fader_settings' class='btn btn-primary btn-block'>Configure Fader Settings</a>");
  client.println("</div>");
  
  client.println("<div class='status-link' style='margin-top: 10px;'>");
  client.println("<a href='/stats' class='btn btn-info btn-block'>View Fader Statistics</a>");
  client.println("</div>");
  
  client.println("</div></div>");
  
  // Debug Tools Card
  client.println("<div class='card' style='margin-top: 20px;'>");
  client.println("<div class='card-header'><h2>Debug Tools</h2></div>");
  client.println("<div class='card-body'>");
  
  client.println("<form method='post' action='/debug'>");
  client.println("<input type='hidden' name='debug' value='0'>");
  client.println("<div class='form-group'>");
  client.println("<div class='checkbox-group'>");
  client.print("<label><input type='checkbox' name='debug' value='1'");
  if (debugMode) client.print(" checked");
  client.println(">Enable Serial Debug Output</label>");
  client.println("</div>");
  client.println("</div>");
  client.println("<button type='submit' class='btn btn-primary btn-block'>Save Debug Setting</button>");
  client.println("</form>");
  
  client.println("<div class='divider'></div>");
  
  client.println("<form method='post' action='/dump'>");
  client.println("<button type='submit' class='btn btn-warning btn-block'>Dump EEPROM to Serial</button>");
  client.println("</form>");
  
  client.println("</div></div>");
  
waitForWriteSpace();

  // Factory Reset Card
  client.println("<div class='card' style='margin-top: 20px;'>");
  client.println("<div class='card-header'><h2>Factory Reset</h2></div>");
  client.println("<div class='card-body'>");
  client.println("<p style='margin-bottom: 16px; color: var(--text-secondary);'>This will reset all settings to factory defaults. Network settings will require a device restart to take effect.</p>");
  client.println("<form method='post' action='/reset_defaults'>");
  client.println("<button type='submit' class='btn btn-danger btn-block' onclick=\"return confirm('Are you sure you want to reset ALL settings to defaults?');\">Reset All Settings</button>");
  client.println("</form>");
  client.println("</div></div>");
  
  client.println("</div>"); // End right column
  client.println("</div>"); // End grid
  client.println("</div>"); // End container
  
  client.println("</body></html>");
}

void waitForWriteSpace() {
  while (client.connected() && client.availableForWrite() < 100) {
    delay(1);
  }
}