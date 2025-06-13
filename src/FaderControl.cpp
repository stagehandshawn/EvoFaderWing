// FaderControl.cpp
#include "FaderControl.h"
#include "NetworkOSC.h"
#include "TouchSensor.h"
#include "WebServer.h"
#include "Utils.h"


//================================
// MOTOR CONTROL
//================================
void driveMotor(Fader& f, int direction) {
  if (direction == 0) {
    // Stop the motor
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, LOW);
    analogWrite(f.pwmPin, 0);
    return;
  }
  
  // Set direction pins
  if (direction > 0) {
    // Move up/forward
    digitalWrite(f.dirPin1, HIGH);
    digitalWrite(f.dirPin2, LOW);
  } else {
    // Move down/backward
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, HIGH);
  }
  
  // Apply PWM speed
  analogWrite(f.pwmPin, Fconfig.defaultPwm);
  
  if (debugMode) {
    debugPrintf("Fader %d: Motor PWM: %d, Dir: %s, Setpoint: %d\n", 
               f.oscID, Fconfig.defaultPwm, direction > 0 ? "UP" : "DOWN", f.setpoint);
  }
}


// Add this new function that accepts custom PWM
void driveMotorWithPWM(Fader& f, int direction, int pwmValue) {
  if (direction == 0) {
    // Stop the motor
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, LOW);
    analogWrite(f.pwmPin, 0);
    return;
  }
  
  // Set direction pins
  if (direction > 0) {
    // Move up/forward
    digitalWrite(f.dirPin1, HIGH);
    digitalWrite(f.dirPin2, LOW);
  } else {
    // Move down/backward
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, HIGH);
  }
  
  // Apply custom PWM speed
  analogWrite(f.pwmPin, pwmValue);
  
  if (debugMode) {
    debugPrintf("Fader %d: Motor PWM: %d, Dir: %s, Setpoint: %d\n", 
               f.oscID, pwmValue, direction > 0 ? "UP" : "DOWN", f.setpoint);
  }
}

// Add this function to calculate PWM based on distance to target
int calculateVelocityPWM(int difference) {
  int absDifference = abs(difference);
  
  // Define PWM ranges
  const int minPWM = 40;   // Minimum PWM to ensure movement (adjust as needed)
  const int maxPWM = Fconfig.defaultPwm;  // Use your existing max PWM
  
  // Define distance thresholds for different speeds
  const int slowZone = 5;   // OSC units - when to start slowing down DEFUALT GOOD MOVMENT WITH THIS UNIT
  const int fastZone = 20;  // OSC units - when to use full speed DEFUALT GOOD MOVMENT WITH THIS UNIT
  
  
  int pwmValue;
  
  if (absDifference >= fastZone) {
    // Far from target - use full speed
    pwmValue = maxPWM;
  } else if (absDifference <= slowZone) {
    // Close to target - use minimum speed
    pwmValue = minPWM;
  } else {
    // In between - linear interpolation
    float ratio = (float)(absDifference - slowZone) / (fastZone - slowZone);
    pwmValue = minPWM + (int)(ratio * (maxPWM - minPWM));
  }
  
  return pwmValue;
}

//================================
// MAIN FADER PROCESSING
//================================




//================================
// MOVE FADER TO SETPOINT
//================================



/// Function to move all faders to their setpoints
// This function will be called when OSC messages are received


void moveAllFadersToSetpoints() {
  bool allFadersAtTarget = false;
  unsigned long moveStartTime = millis();

  while (!allFadersAtTarget) {
    allFadersAtTarget = true; // Assume all are at target until proven otherwise
    

    for (int i = 0; i < NUM_FADERS; i++) {
      Fader& f = faders[i];
      
      // Read current position as OSC value
      int currentOscValue = readFadertoOSC(f);
      
      // Setpoint is already in OSC units (0-100)
      int targetOscValue = (int)f.setpoint;
      
      // Calculate difference in OSC units
      int difference = targetOscValue - currentOscValue;
      
      // Check if we need to move this fader (using a smaller tolerance for OSC units) IF NOT TOUCHING IT
      if (abs(difference) > Fconfig.targetTolerance && !f.touched) {
        allFadersAtTarget = false; // At least one fader is not at target
        
        // Determine direction and move
        // if (difference > 0) {
        //   // Need to move up
        //   driveMotor(f, 1);
        // } else {
        //   // Need to move down
        //   driveMotor(f, -1);
        // }
        

        if (difference > 0) {
          // Need to move up
          int pwm = calculateVelocityPWM(difference);
          driveMotorWithPWM(f, 1, pwm);
        } else {
          // Need to move down  
          int pwm = calculateVelocityPWM(difference);
          driveMotorWithPWM(f, -1, pwm);
        }



        if (debugMode) {
          debugPrintf("Fader %d: Current OSC: %d, Target OSC: %d, Diff: %d\n", 
                     f.oscID, currentOscValue, targetOscValue, difference);
        }

        } else {
          // Fader is at target, stop motor
          driveMotorWithPWM(f, 0, 0);
        }

      // } else {
      //   // Fader is at target, stop motor
      //   driveMotor(f, 0);
      //   f.state = FADER_IDLE;
      // }
    }
    
    // Small delay to prevent overwhelming the system
    delay(5);

    // here we will check for more osc but will block all other functions, so we can get al osc moves
    //handleOscMessage();
    
    // Optional: Add timeout protection to prevent infinite loops
    if (millis() - moveStartTime > 10000) { // 10 second timeout
      // Stop all motors if we've been trying for too long
      for (int i = 0; i < NUM_FADERS; i++) {
        driveMotor(faders[i], 0);
      }
      if (debugMode) {
        debugPrintf("Fader movement timeout - stopping all motors\n");
      }
      break;
    }
  }
  
  if (debugMode && allFadersAtTarget) {
    debugPrintf("All faders have reached their setpoints\n");
  }
}

// Function to set a new setpoint for a specific fader (called when OSC message received)
void setFaderSetpoint(int faderIndex, int oscValue) {
  if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
    // Store the OSC value (0-100) directly as setpoint
    faders[faderIndex].setpoint = constrain(oscValue, 0, 100);
    
    if (debugMode) {
      debugPrintf("Fader %d setpoint set to OSC value: %d\n", 
                 faders[faderIndex].oscID, oscValue);
    }
  }
}


///////////////////////////

void handleFaders() {
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];

    if (!f.touched){    // FOR NOW WE HAVE TO KEEP THIS NOT SURE HOW TO FIX THE FEEDBACK WITHOUT IT YET!!
      continue;
    }

    // Read current position and get OSC value in one call
    int currentOscValue = readFadertoOSC(f);

      // Force send when at top or bottom and ignore rate limiting
    bool forceSend = (currentOscValue == 0 && f.lastReportedValue != 0) ||
                    (currentOscValue == 100 && f.lastReportedValue != 100);

    if (abs(currentOscValue - f.lastReportedValue) >= Fconfig.sendTolerance || forceSend) {
        f.lastReportedValue = currentOscValue;
        
        // If forcesend because fast move to top or bottom then ignore rate limiting
        if (forceSend) {
          sendOscUpdate(f, currentOscValue, true);
        } else {
          sendOscUpdate(f, currentOscValue, false);
        }


        f.setpoint = currentOscValue;

        if (debugMode) {
          debugPrintf("Fader %d position update: %d\n", f.oscID, currentOscValue);
        }
    }
    }
  }





// Read fader analog pin and return OSC value (0-100) using fader's calibrated range, with clamping at both ends
int readFadertoOSC(Fader& f) {
  int analogValue = analogRead(f.analogPin);

  // Clamp near-bottom analog values to force OSC = 0
  if (analogValue <= f.minVal + 15) {
    if (debugMode) {
      //debugPrintf("Fader %d: Clamped to 0 (analog=%d, minVal=%d)\n", f.oscID, analogValue, f.minVal);
    }
    return 0;
  }

  // Clamp near-top analog values to force OSC = 100
  if (analogValue >= f.maxVal - 15) {
    if (debugMode) {
      //debugPrintf("Fader %d: Clamped to 100 (analog=%d, maxVal=%d)\n", f.oscID, analogValue, f.maxVal);
    }
    return 100;
  }

  int oscValue = map(analogValue, f.minVal, f.maxVal, 0, 100);
  return constrain(oscValue, 0, 100);
}
