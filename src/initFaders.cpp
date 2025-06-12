//Fader init

#include "FaderControl.h"
#include "Utils.h"
#include "WebServer.h"

// Calibration timeout in milliseconds
const unsigned long calibrationTimeout = 2000;

//================================
// FADER INITIALIZATION
//================================

void initializeFaders() {
  // Initialize struct array fields
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].analogPin = ANALOG_PINS[i];
    faders[i].pwmPin = PWM_PINS[i];
    faders[i].dirPin1 = DIR_PINS1[i];
    faders[i].dirPin2 = DIR_PINS2[i];
    faders[i].minVal = 20;    //Keep default range small to avoid not being able to hit 0 and 100 percent
    faders[i].maxVal = 1000;  // we might lose a little precision but its better
    faders[i].setpoint = 0;
    faders[i].current = 0;
    faders[i].motorOutput = 0;
    faders[i].lastMotorOutput = 0;
    //faders[i].pidController = nullptr;  //remove for testing and to make it more simple already getting average readings
    faders[i].state = FADER_IDLE;
    faders[i].lastReportedValue = -1;  //remove for testing and to make it more simple already getting average readings
    faders[i].lastMoveTime = 0;
    faders[i].lastOscSendTime = 0;
    faders[i].suppressOSCOut = false;
    faders[i].oscID = OSC_IDS[i];
    
    
    faders[i].lastSentOscValue = -1;
    
    // Initialize color
    faders[i].red = baseBrightness;
    faders[i].green = baseBrightness;
    faders[i].blue = baseBrightness;
    faders[i].colorUpdated = true;

    // Initialize touch timing values
    faders[i].touched = false;
    faders[i].touchStartTime = 0;
    faders[i].touchDuration = 0;
    faders[i].releaseTime = 0;

    // Initialize brightness values
    faders[i].currentBrightness = baseBrightness;
    faders[i].targetBrightness = baseBrightness;
    faders[i].brightnessStartTime = 0;
    faders[i].lastReportedBrightness = 0;
  
  }
}

//================================
// Cconfigure all fader pins
//================================

void configureFaderPins() {
  // Configure pins for each fader
  
  analogReadAveraging(16);  // Sets the ADC to always average 16 samples so we dont need to smooth reading ourselves

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    pinMode(f.pwmPin, OUTPUT);
    pinMode(f.dirPin1, OUTPUT);
    pinMode(f.dirPin2, OUTPUT);
    

    f.setpoint = 64;  // NEW - OSC value set faders to center for testing later will be 0 value
    
    // Initialize state
    f.state = FADER_IDLE;
    f.touched = false;
  }

}



//================================
// CALIBRATION
//================================

void calibrateFaders() {
  debugPrintf("Calibration started at PWM: %d\n", Fconfig.calibratePwm);
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    
    // Set state to calibrating
    f.state = FADER_CALIBRATING;
    
    // ==================== MAX VALUE CALIBRATION ====================
    debugPrintf("Fader %d → Calibrating Max...\n", i);
    analogWrite(f.pwmPin, Fconfig.calibratePwm);
    digitalWrite(f.dirPin1, HIGH); digitalWrite(f.dirPin2, LOW);
    int last = 0, plateau = 0;
    
    // Add timeout for max calibration
    unsigned long startTime = millis();
    bool calibrationSuccess = false;
    
    while (plateau < PLATEAU_COUNT) {
      // Check for timeout (10 seconds)
      if ((millis() - startTime) > calibrationTimeout) {
        debugPrintf("ERROR: Fader %d MAX calibration timed out! Using default value of 1023.\n", i);
        f.maxVal = 1000;  // Use default max value
        calibrationSuccess = false;
        break;  // Exit the loop
      }
      
      int val = analogRead(f.analogPin);
      plateau = (abs(val - last) < PLATEAU_THRESH) ? plateau + 1 : 0;
      last = val;
      delay(10);
      
      pollWebServer();  // Allow web UI to remain responsive
      yield();          // Let MPR121 and Ethernet process in background
      
      // If we reach this point with required plateau count, calibration succeeded
      if (plateau >= PLATEAU_COUNT) {
        calibrationSuccess = true;
        f.maxVal = last - 10;  //subtract a litle value to make sure we can get to top
      }
    }
    
    // Stop motor
    analogWrite(f.pwmPin, 0);
    delay(500);

    // ==================== MIN VALUE CALIBRATION ====================
    debugPrint("→ Calibrating Min...");
    analogWrite(f.pwmPin, Fconfig.calibratePwm);
    digitalWrite(f.dirPin1, LOW); digitalWrite(f.dirPin2, HIGH);
    plateau = 0;
    
    // Reset for min calibration
    startTime = millis();
    bool minCalibrationSuccess = false;
    
    while (plateau < PLATEAU_COUNT) {
      // Check for timeout (10 seconds)
      if ((millis() - startTime) > calibrationTimeout) {
        debugPrintf("ERROR: Fader %d MIN calibration timed out! Using default value of 0.\n", i);
        f.minVal = 20;  // Use default min value
        minCalibrationSuccess = false;
        break;  // Exit the loop
      }
      
      int val = analogRead(f.analogPin);
      plateau = (abs(val - last) < PLATEAU_THRESH) ? plateau + 1 : 0;
      last = val;
      delay(10);

      pollWebServer();  // Allow web UI to remain responsive
      yield();          // Let MPR121 and Ethernet process in background
      
      // If we reach this point with required plateau count, calibration succeeded
      if (plateau >= PLATEAU_COUNT) {
        minCalibrationSuccess = true;
        f.minVal = last + 10;  //Add a litle value to make sure we can get to bottom
      }
    }
    
    // Stop motor
    analogWrite(f.pwmPin, 0);
    
    // Output results with status indicator
    if (calibrationSuccess && minCalibrationSuccess) {
      debugPrintf("→ Calibration Done: Min=%d Max=%d\n", f.minVal, f.maxVal);
    } else {
      debugPrintf("→ Calibration INCOMPLETE for Fader %d: Min=%d Max=%d (Defaults applied where needed)\n", 
                  i, f.minVal, f.maxVal);
    }
    
    // Validate min and max values
    // If min > max or they're too close, use defaults
    if (f.minVal >= f.maxVal || (f.maxVal - f.minVal) < 100) {
      debugPrintf("ERROR: Fader %d has invalid range! Min=%d, Max=%d. Using defaults.\n", 
                  i, f.minVal, f.maxVal);
      f.minVal = 20;
      f.maxVal = 1000;
    }
    
    // Reset fader state to idle
    f.state = FADER_IDLE;
    
    // Reset setpoint
    f.setpoint = analogRead(f.analogPin);
  }
}