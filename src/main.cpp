// EvoFaderWing
// Main.cpp

// Teensy 4.1

#include <Arduino.h>
#include <QNEthernet.h>
#include <LiteOSCParser.h>

#include "Config.h"
#include "EEPROMStorage.h"
#include "TouchSensor.h"
#include "NetworkOSC.h"
#include "NetworkManager.h"
#include "FaderControl.h"
#include "NeoPixelControl.h"
#include "WebServer.h"
#include "Utils.h"
#include "i2cPolling.h"
#include "OLED.h"
#include "Keysend.h"
#include "KeyLedControl.h"

using namespace qindesign::network;
using qindesign::osc::LiteOSCParser;


unsigned long lastI2CPollTime = 0;     // Time of last I2C poll cycle

OLED display;             // define display 
//================================
// MAIN ARDUINO FUNCTIONS
//================================

void setup() {
  // Start the keyboard interface early so the USB device enumerates consistently on Windows
  initKeyboard();

  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 4000) {}
  
  SYSTEM_DEBUG_PRINT("EvoFaderWing init...");

  // Initialize faders
  initializeFaders();
  configureFaderPins();
  
  // Initialize touch sensor (MTCH2120)
  if (!setupTouch()) {
    TOUCH_ERROR_PRINT("Touch sensor init failed!");
  }

  // Start NeoPixels
  setupNeoPixels();
  setupKeyLeds();

    // Check calibration will load calibration data if present ortherwise it will run calibration
  checkCalibration(); 

  // Load configurations from EEPROM
  loadAllConfig();

  moveAllFadersToSetpoints();
  // Calibrate touch sensor once faders are parked at center
  runTouchCalibration();

  //Setup I2C Slaves so we can also check for network reset
  setupI2cPolling();
  
  // Setup OLED before network to watch for no dhcp server and know were booting
  display.setupOLED();

  // Set up network manager and network-facing services
  initNetworkManager();

  // Start web server for configuration
  startWebServer();

  fadeSequence(50,1000); // Startup indication after initialization completes

  //Network reset check
  resetCheckStartTime = millis();

  SYSTEM_DEBUG_PRINT("Initialization complete");

}

void loop() {
  Ethernet.loop();

  static unsigned long lastNetworkServiceMs = 0;
  const unsigned long now = millis();
  const unsigned long networkServiceIntervalMs = networkIsConnected() ? 100UL : 50UL;
  if (now - lastNetworkServiceMs >= networkServiceIntervalMs) {
    lastNetworkServiceMs = now;
    serviceNetwork();
  }

  // Network reset check exiry PRESS 401 5 times during this time for network reset

  if (checkForReset && (millis() - resetCheckStartTime > 5000)) {
    checkForReset = false;
    SYSTEM_DEBUG_PRINT("[RESET] Reset check window expired.");
  }
  
  checkFaderRetry();  // Check for hung fader

  // Check for manual fader movement
  handleFaders();

  // Handle I2C Polling for encoders keypresses and encoder key press
  handleI2c();

  // Process queued OSC packets from UDP callback
  processOscQueue();
  
    
  // Process touch changes 
  if (processTouchChanges()) { updateBrightnessOnFaderTouchChange(); }

  // Check for web requests
  pollWebServer();
  

  // Handle touch sensor errors, no longer needed used for debugging
  if (hasTouchError()) {
    TOUCH_ERROR_PRINT(getLastTouchError().c_str());
    clearTouchError();
  }
  
    // Update NeoPixels
  updateNeoPixels();
  updateKeyLeds();

  // Check for reboot from serial, used for uploading firmware without having to press physical button
  checkSerialForReboot();

}


// oled display functions

void displayIPAddress(){
  display.showIPAddress(networkGetLocalIP(), netConfig.oscPort, netConfig.sendToIP);
}

void displayShowResetHeader(){
  display.clear();
  display.showHeader("Network Reset");
}
