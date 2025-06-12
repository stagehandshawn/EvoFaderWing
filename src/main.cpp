/*
 * Enhanced Teensy 4.1 Motorized Fader Controller
 * PlatformIO Version - Modular Architecture
 * 
 * This is a comprehensive sketch that combines all functionality for controlling
 * motorized faders over Ethernet using OSC protocol.
 * 
 * Features:
 * - OSC communication over Ethernet using QNEthernet
 * - Web server for IP and OSC settings configuration
 * - Smooth motor control with feedback prevention
 * - Calibration routine for fader range mapping
 * - Motor deadzone compensation
 * - Velocity limiting for smooth movements
 * - Position smoothing with moving average filter
 * - OSC feedback prevention
 * - State machine for fader operation
 * - Configuration storage in EEPROM
 * - Touch sensor integration with MPR121
 * - NeoPixel color control with brightness fading
 * - Debugging options
 * 
 * File Structure:
 * - Config.h/cpp: Hardware configuration and global settings
 * - EEPROMStorage.h/cpp: Persistent storage management
 * - TouchSensor.h/cpp: MPR121 touch sensor handling
 * - NetworkOSC.h/cpp: Network setup and OSC communication
 * - FaderControl.h/cpp: Motor control and calibration
 * - NeoPixelControl.h/cpp: LED strip management
 * - WebServer.h/cpp: HTTP configuration interface
 * - Utils.h/cpp: Helper functions and utilities
 */

#include <Arduino.h>
#include <QNEthernet.h>
#include <LiteOSCParser.h>

// Include all our modular components
#include "Config.h"
#include "EEPROMStorage.h"
#include "TouchSensor.h"
#include "NetworkOSC.h"
#include "FaderControl.h"
#include "NeoPixelControl.h"
#include "WebServer.h"
#include "Utils.h"
#include "i2cPolling.h"
#include "OLED.h"

using namespace qindesign::network;
using qindesign::osc::LiteOSCParser;

void updateBrightnessOnFaderTouchChange();

unsigned long lastI2CPollTime = 0;     // Time of last I2C poll cycle

OLED display;             // <-- define display here
IPAddress currentIP;      // <-- define currentIP

//================================
// MAIN ARDUINO FUNCTIONS
//================================

void setup() {
  // Serial setup
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 4000) {}
  
  debugPrint("GMA3 FaderWing init...");

  // Initialize faders
  initializeFaders();
  configureFaderPins();
  
  // Initialize Touch MPR121 
  if (!setupTouch()) {
    debugPrint("Touch sensor initialization failed!");
  }

    // Check calibration will load calibration data if present ortherwise it will run calibration
  checkCalibration(); 

  // Load configurations from EEPROM
  loadAllConfig();

  moveAllFadersToSetpoints();

  //Setup I2C Slaves so we can also check for network reset
  //setupI2cPolling();
  setupI2cPolling();
  
  // Setup OLED before network to watch for no dhcp server and know were booting
  display.setupOLED();

  // Set up network connection
  setupNetwork();


  // display.clear();
  // // Show IP address
  // currentIP = Ethernet.localIP();
  // display.showIPAddress(currentIP);

   displayIPAddress();

  // Start web server for configuration
  startWebServer();
  
  // Start NeoPixels
  setupNeoPixels();

  // Check if we need to run calibration
  checkCalibration();  
  
  //Network reset check
  resetCheckStartTime = millis();

  debugPrint("Initialization complete");
}

void loop() {
  // Network reset check exiry
  if (checkForReset && (millis() - resetCheckStartTime > 5000)) {
    checkForReset = false;
    debugPrint("[RESET] Reset check window expired.");
  }
  
// Process OSC messages
  handleOscMessage();


  // Process faders
  handleFadersSimple();



  // Handle I2C Polling for encoders keypresses and encoder key press
    handleI2c();

    
  // Process touch changes - this function already checks the flag internally
  if (processTouchChanges()) {
    updateBrightnessOnFaderTouchChange();
    printFaderTouchStates();
  }

  // Check for web requests
  pollWebServer();
  

  // Handle touch sensor errors
  if (hasTouchError()) {
    debugPrint(getLastTouchError().c_str());
    clearTouchError();
  }
  
    // Update NeoPixels
  updateNeoPixels();


  // If in debug mode will check for a serial request to reboot into bootloader mode for auto upload without pressing button
  
    checkSerialForReboot();

  yield(); // Let the Teensy do background tasks
}

void displayIPAddress(){

  display.clear();
  // Show IP address
  currentIP = Ethernet.localIP();
  display.showIPAddress(currentIP);

}