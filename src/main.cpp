// EvoFaderWing
// Main.cpp

// Teensy 4.1


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

OLED display;             // define display 
IPAddress currentIP;      // define currentIP

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
  setupI2cPolling();
  
  // Setup OLED before network to watch for no dhcp server and know were booting
  display.setupOLED();

  // Set up network connection
  setupNetwork();

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
  if (checkForReset && (millis() - resetCheckStartTime > 10000)) {
    checkForReset = false;
    debugPrint("[RESET] Reset check window expired.");
  }
  
// Process OSC messages
  handleOscMessage();

  // Check for manual fader movement
  handleFaders();

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

  
  checkSerialForReboot();

  yield(); // Let the Teensy do background tasks
}

void displayIPAddress(){

  display.clear();
  // Show IP address
  currentIP = Ethernet.localIP();
  display.showIPAddress(currentIP,netConfig.receivePort,netConfig.sendToIP,netConfig.sendPort);

}

void displayShowResetHeader(){

  display.showHeader("Network Reset");
}