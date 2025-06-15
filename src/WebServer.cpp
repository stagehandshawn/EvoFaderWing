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
        } else if (request.indexOf("pidKp=") >= 0) {
          requestType = 'P'; // PID settings
          debugPrint("Determined: PID settings");
        } else if (request.indexOf("touchThreshold=") >= 0) {
          requestType = 'T'; // Touch settings
          debugPrint("Determined: Touch settings");
        } else if (request.indexOf("minPwm=") >= 0 || request.indexOf("baseBrightness=") >= 0) {
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
      } else if (path == "/osc_settings") {
        requestType = 'A'; // OSC settings page
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
          
        case 'A': // OSC settings page
          handleOSCSettingsPage();
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
  client.println("<p><a href='/'>Return to home</a></p>");
  client.println("</div></body></html>");
}


void handleDebugToggle(String requestBody) {
  Serial.println("[Toggle] Received /debug POST request");
  Serial.printf("[Toggle] Raw body: %s\n", requestBody.c_str());

  debugMode = (requestBody.indexOf("debug=1") != -1);
  Serial.printf("[Toggle] Debug mode is now: %d\n", debugMode);

    Fconfig.serialDebug = debugMode;
    saveFaderConfig();

  if (!debugMode) display.clearDebugLines();

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
  client.println("<p>Network settings have been saved successfully. For changes to take full effect, you may have to restart the device.</p>");
  client.println("<p><a href='/'>Return to settings</a></p>");
  client.println("</div></body></html>");

  delay(2000);

    // Save to EEPROM
  saveNetworkConfig();

}



void handleOSCSettingsPage() {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html><html><head><title>OSC Settings</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  sendCommonStyles();
  client.println("</head><body>");
  
  sendNavigationHeader("OSC Settings");
  
  client.println("<div class='container'>");
  
  waitForWriteSpace();
  
  client.println("<div class='card'>");
  client.println("<h2>OSC Settings</h2>");
  
  client.println("<form method='get' action='/save'>");
  
  client.println("<label>OSC Send IP</label>");
  client.print("<input type='text' name='osc_sendip' value='");
  client.print(ipToString(netConfig.sendToIP));
  client.println("'>");
  client.println("<p class='help'>IP address of GMA3 console</p>");
  
  client.println("<label>OSC Send Port</label>");
  client.print("<input type='number' name='osc_sendport' value='");
  client.print(netConfig.sendPort);
  client.println("'>");
  
  client.println("<label>OSC Receive Port</label>");
  client.print("<input type='number' name='osc_receiveport' value='");
  client.print(netConfig.receivePort);
  client.println("'>");
  
  client.println("<button type='submit'>Save OSC Settings</button>");
  client.println("</form>");
  
  client.println("<div class='divider'></div>");
  
  client.println("<p><strong>Current Status:</strong></p>");
  client.print("<p>Send to: ");
  client.print(ipToString(netConfig.sendToIP));
  client.print(":");
  client.print(netConfig.sendPort);
  client.println("</p>");
  
  client.print("<p>Receive on: ");
  client.print(ipToString(Ethernet.localIP()));
  client.print(":");
  client.print(netConfig.receivePort);
  client.println("</p>");
  
  client.println("</div>");
  
  client.println("</div>");
  client.println("</body></html>");
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
    
    // Redirect back to fader settings page
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /fader_settings");
    client.println("Connection: close");
    client.println();
  } else {
    sendErrorResponse("Missing calibration PWM parameter");
    return;
  }
}

void handleFaderSettings(String request) {
  debugPrint("Handling fader settings...");
  
  // Extract parameter strings
  String minPwmStr = getParam(request, "minPwm");
  String defaultPwmStr = getParam(request, "defaultPwm");
  String targetToleranceStr = getParam(request, "targetTolerance");
  String sendToleranceStr = getParam(request, "sendTolerance");
  String baseBrightnessStr = getParam(request, "baseBrightness");
  String touchedBrightnessStr = getParam(request, "touchedBrightness");
  
  // Validate and update using constrainParam
  if (minPwmStr.length() > 0) {
    int minPwm = minPwmStr.toInt();
    Fconfig.minPwm = constrainParam(minPwm, 0, 255, Fconfig.minPwm);
  }
  
  if (defaultPwmStr.length() > 0) {
    int defaultPwm = defaultPwmStr.toInt();
    Fconfig.defaultPwm = constrainParam(defaultPwm, 0, 255, Fconfig.defaultPwm);
  }
  
  if (targetToleranceStr.length() > 0) {
    int targetTolerance = targetToleranceStr.toInt();
    Fconfig.targetTolerance = constrainParam(targetTolerance, 0, 100, Fconfig.targetTolerance);
  }
  
  if (sendToleranceStr.length() > 0) {
    int sendTolerance = sendToleranceStr.toInt();
    Fconfig.sendTolerance = constrainParam(sendTolerance, 0, 100, Fconfig.sendTolerance);
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
  
  // Additional logical validation
  if (Fconfig.minPwm > Fconfig.defaultPwm) {
    debugPrint("Warning: Min PWM is greater than Default PWM, swapping values");
    int temp = Fconfig.minPwm;
    Fconfig.minPwm = Fconfig.defaultPwm;
    Fconfig.defaultPwm = temp;
  }

  // Save to EEPROM
  saveFaderConfig();
  
  // Redirect back to fader settings page
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /fader_settings");
  client.println("Connection: close");
  client.println();
}

void handleRunCalibration() {
  debugPrint("Running fader calibration...");
  
  // Run the calibration process
  calibrateFaders();
  saveCalibration();

  // Reinitialize MPR121 after calibration due to I2C hang risk
  debugPrint("Reinitializing touch sensor after calibration...");
  setupTouch();  // Restore I2C communication and config
  
  // Redirect back to fader settings page
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /fader_settings");
  client.println("Connection: close");
  client.println();
}

void handleTouchSettings(String request) {
  debugPrint("Handling touch sensor settings...");
  
  String autoCalModeStr = getParam(request, "autoCalMode");
  String touchThresholdStr = getParam(request, "touchThreshold");
  String releaseThresholdStr = getParam(request, "releaseThreshold");
  
  // Validate and update using constrainParam
  if (autoCalModeStr.length() > 0) {
    int autoCalMode = autoCalModeStr.toInt();
    autoCalibrationMode = constrainParam(autoCalMode, 0, 2, autoCalibrationMode);
  }
  
  if (touchThresholdStr.length() > 0) {
    int threshold = touchThresholdStr.toInt();
    touchThreshold = constrainParam(threshold, 1, 255, touchThreshold);
  }
  
  if (releaseThresholdStr.length() > 0) {
    int threshold = releaseThresholdStr.toInt();
    releaseThreshold = constrainParam(threshold, 1, 255, releaseThreshold);
  }
  
  // Additional logical validation - ensure release < touch
  if (releaseThreshold >= touchThreshold) {
    debugPrint("Warning: Release threshold >= touch threshold, adjusting");
    releaseThreshold = touchThreshold - 1;
    if (releaseThreshold < 1) {
      releaseThreshold = 1;
      touchThreshold = 2;
    }
  }
  
  // Apply the settings to the touch sensor
  setAutoTouchCalibration(autoCalibrationMode);
  manualTouchCalibration();
  
  // Save to EEPROM
  saveTouchConfig();
  
  // Reset MPR121
  setupTouch();
  
  // Redirect back to fader settings page
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /fader_settings");
  client.println("Connection: close");
  client.println();
}

void handleResetDefaults() {
  debugPrint("Resetting all settings to defaults...");
  resetToDefaults();
  sendRedirect();
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
      // NEED TO UPDATE UDP HANDLER
      debugPrintf("Updated OSC Receive Port: %d\n", netConfig.receivePort);
    } else {
      debugPrintf("ERROR: Invalid OSC receive port: %d\n", newReceivePort);
      sendErrorResponse("Invalid OSC receive port (must be 1-65535)");
      return;
    }
  }
  
  // Save to EEPROM

  
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
  client.println("<h1>OSC Settings Saved</h1>");
  client.println("<p>OSC settings have been saved successfully. For changes to take full effect, you may have to restart the device.</p>");
  client.println("<p><a href='/osc_settings'>Return to OSC settings</a></p>");
  client.println("</div></body></html>");

    saveNetworkConfig();
    
    restartUDP();

  debugPrint("OSC settings saved successfully");

}


void handleNetworkReset() {  
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
  client.println("<p><a href='/'>Return to settings</a></p>");
  client.println("</div></body></html>");
  
  debugPrint("Resetting network settings to defaults...");
  resetNetworkDefaults();
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

// Helper function to send navigation header
void sendNavigationHeader(const char* pageTitle) {
  client.println("<div class='header'>");
  client.println("<h1>GMA3 FaderWing Configuration</h1>");
  client.print("<p>IP: ");
  client.print(ipToString(Ethernet.localIP()));
  client.println("</p>");
  client.println("</div>");
  
  client.println("<div class='nav'>");
  client.println("<a href='/'>Network/Debug</a>");
  client.println("<a href='/osc_settings'>OSC</a>");
  client.println("<a href='/fader_settings'>Faders</a>");
  client.println("<a href='/stats'>Statistics</a>");
  client.println("</div>");
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
  client.println("th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }");
  client.println("th { background: #f0f0f0; }");
  client.println("</style>");
  client.println("</head><body>");
  
  sendNavigationHeader("Statistics");
  
  client.println("<div class='container'>");
  client.println("<div class='card'>");
  client.println("<h2>Fader Statistics</h2>");
  
  client.println("<table>");
  client.println("<tr><th>Fader</th><th>Current</th><th>Min</th><th>Max</th><th>OSC Value</th></tr>");
  
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    int currentVal = analogRead(f.analogPin);
    
    client.print("<tr><td>Fader ");
    client.print(i + 1);
    client.print("</td><td>");
    client.print(currentVal);
    client.print("</td><td>");
    client.print(f.minVal);
    client.print("</td><td>");
    client.print(f.maxVal);
    client.print("</td><td>");
    client.print(readFadertoOSC(faders[i]));
    client.println("</td></tr>");
    
    if (i % 3 == 0) waitForWriteSpace();
  }
  
  client.println("</table>");
  client.println("</div>");
  client.println("</div>");
  client.println("</body></html>");
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
  
  // Min PWM
  client.println("<div class='form-group'>");
  client.println("<label>Min PWM</label>");
  client.print("<input type='number' name='minPwm' value='");
  client.print(Fconfig.minPwm);
  client.println("' min='0' max='255'>");
  client.println("<p class='help-text'>Minimum motor speed (too low stalls motor, too high passes setpoint and causes jitter) (0-255)</p>");
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
  client.println(">Normal (More sensitive, faster baseline changes)</option>");
  client.print("<option value='2'");
  if (autoCalibrationMode == 2) client.print(" selected");
  client.println(">Conservative (Defualt, slower baseline changes due to environment)</option>");
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
  client.println("<p class='help-text' style='margin-top: 12px; color: red;'>Do not touch faders while saving</p>");
  client.println("</form></div></div>");
  
  client.println("</div>"); // End container
  client.println("</body></html>");
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
  
  client.println("<div class='container'>");
  
  waitForWriteSpace();
  
  // Network Settings Card
  client.println("<div class='card'>");
  client.println("<h2>Network Settings</h2>");
  
  client.println("<form method='get' action='/save'>");
  
  client.println("<label>");
  client.print("<input type='checkbox' name='dhcp' value='on'");
  if (netConfig.useDHCP) client.print(" checked");
  client.println("> Use DHCP");
  client.println("</label>");
  client.println("<p class='help'>When enabled, static IP settings below are ignored</p>");
  
  client.println("<label>Static IP Address</label>");
  client.print("<input type='text' name='ip' value='");
  client.print(ipToString(netConfig.staticIP));
  client.println("'>");
  
  client.println("<label>Gateway</label>");
  client.print("<input type='text' name='gw' value='");
  client.print(ipToString(netConfig.gateway));
  client.println("'>");
  
  client.println("<label>Subnet Mask</label>");
  client.print("<input type='text' name='sn' value='");
  client.print(ipToString(netConfig.subnet));
  client.println("'>");
  
  client.println("<button type='submit'>Save Network Settings</button>");
  client.println("</form>");
  
  client.println("<form method='post' action='/reset_network'>");
  client.println("<button type='submit' onclick=\"return confirm('Reset network settings?');\">Reset Network</button>");
  client.println("</form>");
  
  client.println("</div>");
  
  waitForWriteSpace();
  
  // Debug Tools Card
  client.println("<div class='card'>");
  client.println("<h2>Debug Tools</h2>");
  
  client.println("<form method='post' action='/debug'>");
  client.println("<input type='hidden' name='debug' value='0'>");
  client.println("<label>");
  client.print("<input type='checkbox' name='debug' value='1'");
  if (debugMode) client.print(" checked");
  client.println("> Enable Serial Debug Output");
  client.println("</label>");
  client.println("<button type='submit'>Save Debug Setting</button>");
  client.println("</form>");
  
  client.println("<div class='divider'></div>");
  
  client.println("<form method='post' action='/dump'>");
  client.println("<button type='submit'>Dump EEPROM to Serial</button>");
  client.println("</form>");
  
  client.println("</div>");
  
  waitForWriteSpace();
  
  // Factory Reset Card
  client.println("<div class='card'>");
  client.println("<h2>Factory Reset</h2>");
  client.println("<p>This will reset all settings to factory defaults.</p>");
  client.println("<form method='post' action='/reset_defaults'>");
  client.println("<button type='submit' onclick=\"return confirm('Reset ALL settings?');\">Reset All Settings</button>");
  client.println("</form>");
  client.println("</div>");
  
  client.println("</div>"); // End container
  client.println("</body></html>");
}

void waitForWriteSpace() {
  while (client.connected() && client.availableForWrite() < 100) {
    delay(1);
  }
}