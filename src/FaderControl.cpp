#include "FaderControl.h"
#include "NetworkOSC.h"
#include "TouchSensor.h"
#include "WebServer.h"
#include "Utils.h"

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
    faders[i].minVal = 0;
    faders[i].maxVal = 1023;
    faders[i].setpoint = 0;
    faders[i].current = 0;
    faders[i].smoothedPosition = 0;
    faders[i].motorOutput = 0;
    faders[i].lastMotorOutput = 0;
    faders[i].pidController = nullptr;
    faders[i].state = FADER_IDLE;
    faders[i].lastReportedValue = -1;
    faders[i].lastMoveTime = 0;
    faders[i].lastOscSendTime = 0;
    faders[i].suppressOSCOut = false;
    faders[i].oscID = OSC_IDS[i];
    
    // Initialize filter
    faders[i].readIndex = 0;
    faders[i].readingsTotal = 0;
    for (int j = 0; j < FILTER_SIZE; j++) {
      faders[i].readings[j] = 0;
    }
    
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

void configureFaderPins() {
  // Configure pins for each fader
  
  analogReadAveraging(16);  // Sets the ADC to always average 16 samples

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    pinMode(f.pwmPin, OUTPUT);
    pinMode(f.dirPin1, OUTPUT);
    pinMode(f.dirPin2, OUTPUT);
    
    // Initialize readings with current value
    int initialReading = analogRead(f.analogPin);
    f.current = initialReading;
    f.smoothedPosition = initialReading;
    f.setpoint = initialReading;
    
    // Initialize moving average filter
    for (int j = 0; j < FILTER_SIZE; j++) {
      f.readings[j] = initialReading;
    }
    f.readingsTotal = initialReading * FILTER_SIZE;
    
    // Setup PID controller with loaded configuration
    f.pidController = new PID(&f.smoothedPosition, &f.motorOutput, &f.setpoint, 
                            Fconfig.pidKp, Fconfig.pidKi, Fconfig.pidKd, DIRECT);
    f.pidController->SetMode(AUTOMATIC);
    f.pidController->SetSampleTime(PID_SAMPLE_TIME);
    f.pidController->SetOutputLimits(-Fconfig.defaultPwm, Fconfig.defaultPwm);
    
    // Initialize state
    f.state = FADER_IDLE;
    f.touched = false;
  }
}

//================================
// POSITION READING AND FILTERING
//================================

int readSmoothedPosition(Fader& f) {
  // Subtract the oldest reading
  f.readingsTotal -= f.readings[f.readIndex];
  
  // Read from the sensor
  f.readings[f.readIndex] = analogRead(f.analogPin);
  
  // Add the new reading to the total
  f.readingsTotal += f.readings[f.readIndex];
  
  // Advance to the next position in the array
  f.readIndex = (f.readIndex + 1) % FILTER_SIZE;
  
  // Calculate the average
  return f.readingsTotal / FILTER_SIZE;
}

//================================
// STATE MANAGEMENT
//================================

void updateFaderState(Fader& f) {
  switch (f.state) {
    case FADER_IDLE:
      // Don't automatically go to MOVING state based on position
      // Only OSC messages should trigger state change to MOVING
      break;
      
    case FADER_MOVING:
      // Check if we've reached the target
      if (abs(f.smoothedPosition - f.setpoint) <= Fconfig.targetTolerance) {
        f.state = FADER_IDLE;
        debugPrintf("Fader state: MOVING → IDLE (target reached)\n");
        
        // Stop the motor
        driveMotor(f, 0);
        
        // Send final position update
        int oscValue = map((int)f.smoothedPosition, f.minVal, f.maxVal, 0, 127);
        oscValue = constrain(oscValue, 0, 127);
        sendOscUpdate(f, oscValue, true);
      }
      break;
      
    case FADER_CALIBRATING:
      // Calibration state is handled by the calibration function
      break;
      
    case FADER_ERROR:
      // Error state - could implement recovery logic here
      break;
  }
}

//================================
// MOTOR CONTROL
//================================

void driveMotor(Fader& f, double pwmOut) {
  // Apply deadzone compensation
  int pwm = 0;
  if (abs(pwmOut) > 0) {
    // If output is non-zero, apply minimum PWM needed to overcome motor inertia
    pwm = map(abs(pwmOut), 0, Fconfig.defaultPwm, Fconfig.motorDeadzone, Fconfig.defaultPwm);
    pwm = constrain(pwm, Fconfig.motorDeadzone, Fconfig.defaultPwm);
  }
  
  // Set direction pins based on desired direction
  digitalWrite(f.dirPin1, pwmOut > 1);
  digitalWrite(f.dirPin2, pwmOut < -1);
  
  // Apply PWM to control speed
  analogWrite(f.pwmPin, pwm);
  
  if (debugMode && pwm > 0) {
    debugPrintf("Motor PWM: %d, Dir: %s\n", pwm, pwmOut > 0 ? "UP" : "DOWN");
  }
}

//================================
// MAIN FADER PROCESSING
//================================

void handleFaders() {
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    
    // Read raw position
    f.current = analogRead(f.analogPin);
    
    // Get smoothed position
    f.smoothedPosition = readSmoothedPosition(f);
    
    // Check for significant movement (for reporting)
    if (abs(f.smoothedPosition - f.lastReportedValue) >= Fconfig.sendTolerance) {
      f.lastReportedValue = f.smoothedPosition;
      
      // Map the current position to 0-127 for OSC
      int oscValue = map((int)f.smoothedPosition, f.minVal, f.maxVal, 0, 127);
      oscValue = constrain(oscValue, 0, 127);
      
      // Send OSC update (with rate limiting built in)
      sendOscUpdate(f, oscValue, false);
    }

    // Update state machine
    updateFaderState(f);
    
    // Handle motor control based on state
    if (f.state == FADER_MOVING) {
      // Compute PID output
      f.pidController->Compute();
      
      // Apply velocity limiting for smooth acceleration/deceleration
      double targetOutput = f.motorOutput;
      double currentOutput = f.lastMotorOutput;
      
      if (abs(targetOutput - currentOutput) > MAX_VELOCITY_CHANGE) {
        if (targetOutput > currentOutput) {
          currentOutput += MAX_VELOCITY_CHANGE;
        } else {
          currentOutput -= MAX_VELOCITY_CHANGE;
        }
      } else {
        currentOutput = targetOutput;
      }
      
      // Save for next iteration
      f.lastMotorOutput = currentOutput;
      
      // Drive the motor with velocity-limited output
      driveMotor(f, currentOutput);
    }
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
        f.maxVal = 1023;  // Use default max value
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
        f.maxVal = last - 5;  //subtract a litle value to make sure we can get to top
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
        f.minVal = 0;  // Use default min value
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
        f.minVal = last + 5;  //Add a litle value to make sure we can get to bottom
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
      f.minVal = 0;
      f.maxVal = 1023;
    }
    
    // Reset fader state to idle
    f.state = FADER_IDLE;
    
    // Reset filter with current value
    int currentReading = analogRead(f.analogPin);
    for (int j = 0; j < FILTER_SIZE; j++) {
      f.readings[j] = currentReading;
    }
    f.readingsTotal = currentReading * FILTER_SIZE;
    f.smoothedPosition = currentReading;
    f.setpoint = currentReading;
  }
}

//================================
// MOVE FADER TO SETPOINT
//================================

void moveFaderToSetpoint(Fader& f) {
  const unsigned long timeout = 10000;          // Safety timeout in ms
  unsigned long startTime = millis();

  // Suppress OSC output until manual movement occurs
  f.suppressOSCOut = true;

  while (true) {
    // Safety timeout
    if ((millis() - startTime) > timeout) {
      debugPrintf("Fader move timed out\n");
      driveMotor(f, 0);
      break;
    }

    // Update position
    f.current = analogRead(f.analogPin);
    f.smoothedPosition = readSmoothedPosition(f);

    // Check if at target
    if (abs(f.smoothedPosition - f.setpoint) <= Fconfig.targetTolerance) {
      driveMotor(f, 0);
      break;
    }

    // Compute PID
    f.pidController->Compute();

    // Apply velocity limiting
    double targetOutput = f.motorOutput;
    double currentOutput = f.lastMotorOutput;
    if (abs(targetOutput - currentOutput) > MAX_VELOCITY_CHANGE) {
      currentOutput += (targetOutput > currentOutput) ? MAX_VELOCITY_CHANGE : -MAX_VELOCITY_CHANGE;
    } else {
      currentOutput = targetOutput;
    }
    f.lastMotorOutput = currentOutput;

    // Drive motor
    driveMotor(f, currentOutput);

    // Maintain responsiveness
    pollWebServer();
    yield();
  }

  f.state = FADER_IDLE;
}