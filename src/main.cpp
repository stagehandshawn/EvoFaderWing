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

using namespace qindesign::network;
using qindesign::osc::LiteOSCParser;

//================================
// MAIN ARDUINO FUNCTIONS
//================================

void setup() {
  // Serial setup
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 4000) {}
  
  debugPrint("Teensy 4.1 Motorized Fader Controller initializing...");

  // Initialize faders
  initializeFaders();
  configureFaderPins();

  // Initialize Touch MPR121 
  if (!setupTouch()) {
    debugPrint("Touch sensor initialization failed!");
  }

  // Load configurations from EEPROM
  loadAllConfig();
   
  // Set up network connection
  setupNetwork();
  
  // Start web server for configuration
  startWebServer();
  
  // Start NeoPixels
  setupNeoPixels();

  // Check if we need to run calibration
  // checkCalibration();  // COMMENTED OUT FOR TESTING - REENABLE WHEN NEEDED
   
  debugPrint("Initialization complete");
}

void loop() {
  // Check for web requests
  pollWebServer();
  
  // Process OSC messages
  int size = udp.parsePacket();
  if (size > 0) {
    const uint8_t *data = udp.data();
    LiteOSCParser parser;

    if (parser.parse(data, size)) {
      const char* addr = parser.getAddress();
      
      // Check if it's a color message
      if (strstr(addr, "/Color") != NULL) {
        if (parser.getTag(0) == 's') {
          handleColorOsc(addr, parser.getString(0));
        }
      } 
      // Otherwise, treat it as a fader position message
      else {
        int value = parser.getInt(0);
        handleOscPacket(addr, value);
      }
    } else {
      debugPrint("Invalid OSC message.");
    }
  }

  // Process touch changes - this function already checks the flag internally
  if (processTouchChanges()) {
    // Trigger NeoPixel brightness transition on touch change
    for (int i = 0; i < NUM_FADERS; i++) {
      Fader& f = faders[i];
      bool currentTouch = f.touched;
      static bool previousTouch[NUM_FADERS] = { false };  // Keep previous touch state

      if (currentTouch != previousTouch[i]) {
        f.brightnessStartTime = millis();
        f.targetBrightness = currentTouch ? touchedBrightness : baseBrightness;

        debugPrintf("Fader %d → Touch %s → Brightness target = %d", i,
                    currentTouch ? "TOUCHED" : "released",
                    f.targetBrightness);

        previousTouch[i] = currentTouch;
      }
    }
    
    // If true is returned, touch states were updated
    // Print the current touch states
    printFaderTouchStates();
  }

  // Process faders
  handleFaders();
  
  // Update NeoPixels
  updateNeoPixels();

  // Handle touch sensor errors
  if (hasTouchError()) {
    debugPrint(getLastTouchError().c_str());
    clearTouchError();
  }

  yield(); // Let the Teensy do background tasks
}