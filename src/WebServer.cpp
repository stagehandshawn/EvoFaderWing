#include "WebServer.h"
#include "Utils.h"
#include "EEPROMStorage.h"
#include "FaderControl.h"
#include "TouchSensor.h"
#include <QNEthernet.h>

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
      
      // Determine the request type based on method and path
        if (path.startsWith("/save")) {
          if (request.indexOf("ip=") > 0) {
            requestType = 'N'; // Network settings
          } else if (request.indexOf("calib_pwm=") > 0) {
            requestType = 'C'; // Calibration settings
          } else if (request.indexOf("pidKp=") > 0) {
            requestType = 'P'; // PID settings
          } else if (request.indexOf("touchThreshold=") > 0) {
            requestType = 'T'; // Touch settings
          } else if (request.indexOf("motorDeadzone=") > 0) {
            requestType = 'F'; // Fader settings
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
        } else if (path == "/") {
          requestType = 'H'; // Home/Root page
        }
      
      // Handle the request based on its type
      switch (requestType) {
        case 'N': // Network settings
          handleNetworkSettings(request);
          break;
          
        case 'C': // Calibration settings
          handleCalibrationSettings(request);
          break;
          
        case 'P': // PID settings
          handlePIDSettings(request);
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
          
        default: // 404 or unrecognized request
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
  
  // Extract and parse network parameters
  netConfig.useDHCP     = request.indexOf("dhcp=") > 0;
  netConfig.staticIP    = stringToIP(getParam(request, "ip"));
  netConfig.gateway     = stringToIP(getParam(request, "gw"));
  netConfig.subnet      = stringToIP(getParam(request, "sn"));
  netConfig.sendToIP    = stringToIP(getParam(request, "sendip"));
  netConfig.receivePort = getParam(request, "rxport").toInt();
  netConfig.sendPort    = getParam(request, "txport").toInt();

  // Save to EEPROM
  saveNetworkConfig();
  
  // Send special response for network settings - they need a restart to take effect
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body><h1>Network Settings Saved</h1>");
  client.println("<p>The settings have been saved. For network changes to take full effect, please restart the device.</p>");
  client.println("<p>If only OSC settings were changed you may be able to continue without restart.</p>");
  client.println("<p><a href='/'>Return to settings</a></p></body></html>");
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

void handlePIDSettings(String request) {
  debugPrint("Handling PID settings...");
  
  // Extract and parse PID parameters only
  Fconfig.pidKp = getParam(request, "pidKp").toFloat();
  Fconfig.pidKi = getParam(request, "pidKi").toFloat();
  Fconfig.pidKd = getParam(request, "pidKd").toFloat();
  
  // Update PID controllers with new values
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].pidController->SetTunings(Fconfig.pidKp, Fconfig.pidKi, Fconfig.pidKd);
    // Keep the output limits (using existing config.defaultPwm)
    faders[i].pidController->SetOutputLimits(-Fconfig.defaultPwm, Fconfig.defaultPwm);
  }
  
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
  
  // Update PID output limits with new defaultPwm
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].pidController->SetOutputLimits(-Fconfig.defaultPwm, Fconfig.defaultPwm);
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
  
  // Special response for network settings
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body><h1>Network Settings Reset</h1>");
  client.println("<p>Network settings have been reset to defaults. For changes to take full effect, please restart the device.</p>");
  client.println("<p><a href='/'>Return to settings</a></p></body></html>");
}

void send404Response() {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body></html>");
}

void sendRedirect() {
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}

//================================
// MAIN WEB PAGE HANDLER
//================================

void handleRoot() {
  String html;
  html += "<!DOCTYPE html><html><head><title>Fader Configuration</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 0; padding: 0; }";
  html += ".main { display: flex; flex-direction: row; padding: 20px; }";
  html += ".left, .right { padding: 20px; }";
  html += ".left { flex: 1 1 60%; max-width: 600px; }";
  //html += ".right { flex: 1 1 40%; min-width: 300px; background-color: #f9f9f9; border-left: 1px solid #ccc; }";
  html += ".right { flex: 1 1 40%; min-width: 300px; background-color: #f9f9f9; border-left: 1px solid #ccc; overflow-y: auto; max-height: 90vh; }";
  html += "fieldset { margin-bottom: 20px; border: 1px solid #ccc; padding: 10px; }";
  html += "legend { font-weight: bold; }";
  html += "label { display: block; margin-top: 10px; }";
  html += "input, select { margin-bottom: 10px; padding: 5px; width: 100%; }";
  html += "small { display: block; color: #555; margin-bottom: 10px; }";
  html += "h1 { padding: 20px; }";
  html += "</style></head><body>";

  html += "<h1>Motorized Fader Configuration</h1>";
  html += "<div class='main'>";

  // LEFT SETTINGS PANEL
  html += "<div class='left'>";

  // Network configuration
  html += "<fieldset><legend>Network Settings</legend>";
  html += "<form method='get' action='/save'>";
  html += "<label>Use DHCP: <input type='checkbox' name='dhcp'";
  if (netConfig.useDHCP) html += " checked";
  html += "></label>";
  html += "<label>Static IP: <input name='ip' value='" + ipToString(netConfig.staticIP) + "'></label>";
  html += "<label>Gateway: <input name='gw' value='" + ipToString(netConfig.gateway) + "'></label>";
  html += "<label>Subnet: <input name='sn' value='" + ipToString(netConfig.subnet) + "'></label>";
  html += "<label>Send-To IP: <input name='sendip' value='" + ipToString(netConfig.sendToIP) + "'></label>";
  html += "<label>Receive Port: <input name='rxport' value='" + String(netConfig.receivePort) + "' type='number'></label>";
  html += "<label>Send Port: <input name='txport' value='" + String(netConfig.sendPort) + "' type='number'></label>";
  html += "<input type='submit' value='Save Network Settings'>";
  html += "</form>";

  html += "<form method='post' action='/reset_network'>";
  html += "<input type='submit' value='Reset Network Settings' onclick=\"return confirm('Reset network settings?');\">";
  html += "</form></fieldset>";

  // OSC Configuration
  html += "<fieldset><legend>OSC Settings</legend>";
  html += "<form method='get' action='/save'>";
  html += "<label>OSC Send IP: <input name='sendip' value='" + ipToString(netConfig.sendToIP) + "'></label>";
  html += "<label>OSC Send Port: <input name='txport' type='number' value='" + String(netConfig.sendPort) + "'></label>";
  html += "<label>OSC Receive Port: <input name='rxport' type='number' value='" + String(netConfig.receivePort) + "'></label>";
  html += "<input type='submit' value='Save OSC Settings'>";
  html += "</form></fieldset>";

  // Fader settings
  html += "<fieldset><legend>Fader Settings</legend>";
  html += "<form method='get' action='/save'>";
  html += "<label>Motor Deadzone: <input type='number' name='motorDeadzone' value='" + String(Fconfig.motorDeadzone) + "'>";
  html += "<small>Minimum error allowed before motor stops (prevents jitter).</small></label>";

  html += "<label>Default PWM: <input type='number' name='defaultPwm' value='" + String(Fconfig.defaultPwm) + "'>";
  html += "<small>Base speed used for fader movement (range: 0–255).</small></label>";

  html += "<label>Target Tolerance: <input type='number' name='targetTolerance' value='" + String(Fconfig.targetTolerance) + "'>";
  html += "<small>How close to target position before stopping the motor.</small></label>";

  html += "<label>Send Tolerance: <input type='number' name='sendTolerance' value='" + String(Fconfig.sendTolerance) + "'>";
  html += "<small>How much the fader must move before sending OSC update.</small></label>";

  html += "<input type='submit' value='Save Fader Settings'>";
  html += "</form>";

  // PID Configuration Subsection
  html += "<legend>PID Configuration</legend>";
  html += "<form method='get' action='/save'>";
  html += "<label>P Gain: <input type='number' step='0.1' name='pidKp' value='" + String(Fconfig.pidKp) + "'>";
  html += "<small>Proportional gain — how strongly the fader reacts to error.</small></label>";

  html += "<label>I Gain: <input type='number' step='0.1' name='pidKi' value='" + String(Fconfig.pidKi) + "'>";
  html += "<small>Integral gain — helps correct steady errors over time.</small></label>";

  html += "<label>D Gain: <input type='number' step='0.01' name='pidKd' value='" + String(Fconfig.pidKd) + "'>";
  html += "<small>Derivative gain — helps dampen fast changes.</small></label>";

  html += "<input type='submit' value='Save PID Settings'>";
  html += "</form>";

  // Calibration PWM Subsection
  html += "<legend>Calibration Settings</legend>";
  html += "<form method='get' action='/save'>";
  html += "<label>Calibration PWM: <input name='calib_pwm' type='number' value='" + String(Fconfig.calibratePwm) + "'>";
  html += "<small>Motor speed during calibration — low value for gentle sweep.</small></label>";
  html += "<input type='submit' value='Save Calibration Settings'>";
  html += "</form>";

  // Calibration trigger
  html += "<form method='post' action='/calibrate'>";
  html += "<input type='hidden' name='calibrate' value='1'>";
  html += "<input type='submit' value='Run Fader Calibration'>";
  html += "</form></fieldset>";

  // Touch Sensor Settings
  html += "<fieldset><legend>Touch Sensor Settings</legend>";
  html += "<form method='get' action='/save'>";
  html += "<label>Auto Calibration Mode: <select name='autoCalMode'>";
  html += "<option value='0'" + String(autoCalibrationMode == 0 ? " selected" : "") + ">Disabled - use only after manual calibration</option>";
  html += "<option value='1'" + String(autoCalibrationMode == 1 ? " selected" : "") + ">Normal - Updates baseline over time, good for typical environments</option>";
  html += "<option value='2'" + String(autoCalibrationMode == 2 ? " selected" : "") + ">Conservative - Updates very slowly, best for noisy or unstable conditions</option>";
  html += "</select></label>";

  html += "<label>Touch Threshold: <input type='number' name='touchThreshold' value='" + String(touchThreshold) + "'>";
  html += "<small>Higher = less sensitive (default: 12)</small></label>";

  html += "<label>Release Threshold: <input type='number' name='releaseThreshold' value='" + String(releaseThreshold) + "'>";
  html += "<small>Lower = harder to release (default: 6)</small></label>";

  html += "<input type='submit' value='Save Touch Settings (do not touch faders)'>";
  html += "</form></fieldset>";

  // Debug + EEPROM Tools
  html += "<fieldset><legend>Debug Tools</legend>";
  html += "<form method='post' action='/debug'>";
  html += "<input type='hidden' name='debug' value='0'>";
  html += "<label><input type='checkbox' name='debug' value='1'";
  if (debugMode) html += " checked";
  html += "> Enable Serial Debug Output</label>";
  html += "<input type='submit' value='Save'>";
  html += "</form>";

  html += "<form method='post' action='/dump'>";
  html += "<input type='submit' value='Dump EEPROM to Serial'>";
  html += "</form></fieldset>";

  // Factory Reset
  html += "<fieldset><legend>Reset All Settings</legend>";
  html += "<form method='post' action='/reset_defaults'>";
  html += "<input type='submit' value='Reset to Defaults' onclick=\"return confirm('Are you sure you want to reset all settings?');\">";
  html += "</form></fieldset>";

  html += "</div>"; // END LEFT

  // RIGHT FADER STATUS PANEL
  html += "<div class='right'>";
  html += "<fieldset><legend>Fader Status</legend>";


  // for (int i = 0; i < NUM_FADERS; i++) {
  //   Fader& f = faders[i];
  //   html += "<p><b>Fader " + String(i) + ":</b><br>";
  //   html += "Min: " + String(f.minVal) + "<br>";
  //   html += "Max: " + String(f.maxVal) + "<br>";
  //   html += "Current: " + String((int)f.smoothedPosition) + "<br>";
  //   html += "State: " + String(f.state) + "</p><hr>";
  // }

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    html += "<div class='fader-status'>";
    html += "<h4>Fader " + String(i) + "</h4>";
    html += "<div class='fader-info'>";
    html += "Min: " + String(f.minVal) + " | Max: " + String(f.maxVal) + "<br>";
    html += "Current: " + String((int)f.smoothedPosition) + " | State: " + String(f.state);
    html += "</div></div>";
  }


  html += "</fieldset>";
  html += "</div>"; // END RIGHT

  html += "</div>"; // END MAIN
  html += "</body></html>";

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.print(html);
}