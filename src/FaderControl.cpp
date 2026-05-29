// FaderControl.cpp
#include "FaderControl.h"
#include "NetworkOSC.h"
#include "TouchSensor.h"
#include "WebServer.h"
#include "Utils.h"
#include "NeoPixelControl.h"

bool FaderRetryPending = false;
unsigned long FaderRetryTime = 0;
static bool FaderMoveActive = false;
static const uint8_t MANUAL_DETECT_DELTA = 1;
static const unsigned long MANUAL_OVERRIDE_HOLD_MS = 500;
static const unsigned long REMOTE_CONTROL_LOCKOUT_MS = 800;

static inline bool allowFaderOscWithoutTouchMode() {
  return Fconfig.allowFaderOscWithoutTouch;
}

//================================
// MOTOR CONTROL
//================================


void driveMotorWithPWM(Fader& f, int direction, int pwmValue) {
  if (!f.motorEnabled) {
    // Keep motor off when disabled
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, LOW);
    analogWrite(f.pwmPin, 0);
    return;
  }

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
}

int calculateVelocityPWM(int difference) {
  int absDifference = abs(difference);
  
  // Define PWM ranges
  const int minPWM = Fconfig.minPwm;   // Minimum PWM to ensure movement (adjust as needed)
  const int maxPWM = Fconfig.maxPwm;  // Use the configured maximum PWM
  
  // Define distance thresholds for different speeds
  int slowZone = Fconfig.slowZone;   // OSC units - start slowing earlier for smoother approach
  int fastZone = Fconfig.fastZone;   // OSC units - when to use full speed
  // Guard against invalid values (OSC is 0-100)
  if (slowZone < 0) slowZone = 0;
  if (fastZone < 0) fastZone = 0;
  if (slowZone > 100) slowZone = 100;
  if (fastZone > 100) fastZone = 100;
  if (fastZone <= slowZone) {
    slowZone = SLOW_ZONE;
    fastZone = FAST_ZONE;
  }
  
  
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
// MOVE ALL FADERs TO SETPOINT
//================================

void moveAllFadersToSetpoints() {
  if (FaderMoveActive) {
    // Already running; let the current pass finish using updated setpoints
    return;
  }
  FaderMoveActive = true;

  bool allFadersAtTarget = false;
  unsigned long moveStartTime = millis();

  while (!allFadersAtTarget) {
    allFadersAtTarget = true; // Assume all are at target until proven otherwise
    

    for (int i = 0; i < NUM_FADERS; i++) {
      Fader& f = faders[i];

      if (!f.motorEnabled) {
        // Disabled motors are treated as parked; ensure power is off
        driveMotorWithPWM(f, 0, 0);
        continue;
      }
      
      // Read current position as OSC value
      int currentOscValue = readFadertoOSC(f);
      
      
      // Calculate difference in OSC units
      int difference = f.setpoint - currentOscValue;
      
      // Check if we need to move this fader (using a smaller tolerance for OSC units) IF NOT TOUCHING IT
      bool manualOwnershipActive = allowFaderOscWithoutTouchMode() && f.manualOverride;
      if (abs(difference) > Fconfig.targetTolerance && !f.touched && !manualOwnershipActive) {
        allFadersAtTarget = false; // At least one fader is not at target
        
        if (difference > 0) {
          // Need to move up
          int pwm = calculateVelocityPWM(difference);
          driveMotorWithPWM(f, 1, pwm);
        } else {
          // Need to move down  
          int pwm = calculateVelocityPWM(difference);
          driveMotorWithPWM(f, -1, pwm);
        }

        } else {
          // Fader is at target, stop motor
          driveMotorWithPWM(f, 0, 0);
        }

    }
    
    // Yield to prevent overwhelming the system
    yield();
    
    // Add timeout protection to prevent infinite loops

    if (millis() - moveStartTime > FADER_MOVE_TIMEOUT) {
      // Stop all motors and flash red on faders that didn't reach target
      bool failed[NUM_FADERS] = {false};
      bool retryNeeded = false;
      uint8_t origColors[NUM_FADERS][3] = {{0}};
      uint8_t scaledRed = (uint8_t)((255UL * Fconfig.touchedBrightness) / 255UL);

      for (int i = 0; i < NUM_FADERS; i++) {
        Fader& f = faders[i];
        driveMotorWithPWM(f, 0, 0);

        if (!f.motorEnabled) {
          continue;
        }

        int currentOscValue = readFadertoOSC(f);
        int difference = f.setpoint - currentOscValue;
        bool manualOwnershipActive = allowFaderOscWithoutTouchMode() && f.manualOverride;
        if (abs(difference) > Fconfig.targetTolerance && !f.touched && !manualOwnershipActive) {
          failed[i] = true;
          origColors[i][0] = f.red;
          origColors[i][1] = f.green;
          origColors[i][2] = f.blue;
        }
      }

      // Flash all failed faders together (full strip red) without using
      for (int flash = 0; flash < 3; flash++) {
        for (int i = 0; i < NUM_FADERS; i++) {
          if (!failed[i]) continue;
          for (int j = 0; j < PIXELS_PER_FADER; j++) {
            pixels.setPixelColor(i * PIXELS_PER_FADER + j, pixels.Color(scaledRed, 0, 0));
          }
        }
        pixels.show();
        delay(150);

        for (int i = 0; i < NUM_FADERS; i++) {
          if (!failed[i]) continue;
          for (int j = 0; j < PIXELS_PER_FADER; j++) {
            pixels.setPixelColor(i * PIXELS_PER_FADER + j, pixels.Color(origColors[i][0], origColors[i][1], origColors[i][2]));
          }
        }
        pixels.show();
        delay(50);
      }
      
      // Track failures and disable motors that repeatedly time out
      unsigned long failureTime = millis();
      for (int i = 0; i < NUM_FADERS; i++) {
        if (!failed[i]) {
          continue;
        }

        Fader& f = faders[i];
        f.failureCount++;
        f.lastFailureTime = failureTime;

        if (f.failureCount >= FADER_MAX_FAILURES) {
          f.motorEnabled = false;
          f.red = 255;
          f.green = 0;
          f.blue = 0;
          FADER_ERROR_PRINTF("Fader %d disabled after %u consecutive failures", f.oscID, f.failureCount);
        } else {
          retryNeeded = true;
        }
      }
      
      // Set retry flag for remaining enabled faders that still need movement
      if (retryNeeded) {
        FaderRetryPending = true;
        FaderRetryTime = millis() + RETRY_INTERVAL;
      } else {
        FaderRetryPending = false;
      }
      
      if (retryNeeded) {
        FADER_ERROR_PRINTF("Fader movement timeout - will retry in %lu seconds", RETRY_INTERVAL / 1000);
      } else {
        FADER_ERROR_PRINT("Fader movement timeout - disabling stuck fader(s) with no retry");
      }
      break;
    }

  }
  
  if (allFadersAtTarget) {
    for (int i = 0; i < NUM_FADERS; i++) {
      if (faders[i].motorEnabled) {
        faders[i].failureCount = 0;
      }
    }
    FaderRetryPending = false;
  }
  FaderMoveActive = false;
}

// Function to set a new setpoint for a specific fader (called when OSC message received)
void setFaderSetpoint(int faderIndex, int oscValue) {
  if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
    Fader& f = faders[faderIndex];
    f.setpoint = constrain(oscValue, 0, 100);

    if (allowFaderOscWithoutTouchMode()) {
      f.manualOverride = false;
      f.remoteControlLockoutUntil = millis() + REMOTE_CONTROL_LOCKOUT_MS;
    } else {
      f.manualOverride = false;
      f.remoteControlLockoutUntil = 0;
    }
    
    FADER_DEBUG_PRINTF("Setpoint updated: fader %d -> %d", f.oscID, f.setpoint);
  }
}



void handleFaders() {
  unsigned long now = millis();
  bool allowUntouchedOsc = allowFaderOscWithoutTouchMode();

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];

    int currentOscValue = readFadertoOSC(f);

    if (f.lastSampledOscValue < 0) {
      f.lastSampledOscValue = currentOscValue;
    }

    if (!allowUntouchedOsc) {
      f.manualOverride = false;
      f.remoteControlLockoutUntil = 0;
      f.lastSampledOscValue = currentOscValue;

      if (!f.touched) {
        continue;
      }
    } else {
      int sampledDelta = abs(currentOscValue - f.lastSampledOscValue);
      bool localMovement = (sampledDelta >= MANUAL_DETECT_DELTA);
      bool remoteLockoutActive = (now < f.remoteControlLockoutUntil);

      if (f.touched) {
        f.manualOverride = false;
      } else {
        if (!FaderMoveActive && !calibrationInProgress && !remoteLockoutActive && localMovement) {
          if (!f.manualOverride) {
            FADER_DEBUG_PRINTF("Fader %d manual override ON", f.oscID);
          }
          f.manualOverride = true;
          f.manualLastMoveTime = now;
          f.setpoint = currentOscValue;
        } else if (f.manualOverride) {
          if (localMovement) {
            f.manualLastMoveTime = now;
            f.setpoint = currentOscValue;
          } else if (now - f.manualLastMoveTime >= MANUAL_OVERRIDE_HOLD_MS) {
            f.manualOverride = false;
            FADER_DEBUG_PRINTF("Fader %d manual override OFF", f.oscID);
          }
        }
      }

      if (!f.touched && !f.manualOverride) {
        f.lastSampledOscValue = currentOscValue;
        continue;
      }
    }

    // Force send when at top or bottom and ignore rate limiting
    bool forceSend = (currentOscValue == 0 && f.lastReportedValue != 0) ||
                    (currentOscValue == 100 && f.lastReportedValue != 100);

    if (abs(currentOscValue - f.lastReportedValue) >= Fconfig.sendTolerance || forceSend) {
        f.lastReportedValue = currentOscValue;
        
        // If forcesend because fast move to top or bottom then ignore rate limiting
        if (forceSend) {
          sendFaderOsc(f, currentOscValue, true);
        } else {
          sendFaderOsc(f, currentOscValue, false);
        }
        
        f.setpoint = currentOscValue;

    }

    f.lastSampledOscValue = currentOscValue;
  }
}





// Read fader analog pin and return OSC value (0-100) using fader's calibrated range, with clamping at both ends
int readFadertoOSC(Fader& f) {
  int analogValue = analogRead(f.analogPin);

  // Suppress tiny jitter in the raw reading to avoid 0/1 flicker in OSC
  if (f.lastAnalogValue >= 0) {
    if (abs(analogValue - f.lastAnalogValue) <= ANALOG_NOISE_TOLERANCE) {
      analogValue = f.lastAnalogValue;
    } else {
      f.lastAnalogValue = analogValue;
    }
  } else {
    f.lastAnalogValue = analogValue;
  }

  // Clamp near-bottom analog values to force OSC = 0
  if (analogValue <= f.minVal + 4) {
    return 0;
  }

  // Clamp near-top analog values to force OSC = 100
  if (analogValue >= f.maxVal - 4) {
    return 100;
  }

  int oscValue = map(analogValue, f.minVal, f.maxVal, 0, 100);
  return constrain(oscValue, 0, 100);
}




void resetFaderOwnership() {
  unsigned long lockoutUntil = millis() + 500; // 500ms window for setpoints to arrive
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].manualOverride = false;
    faders[i].remoteControlLockoutUntil = lockoutUntil;
  }
}

void sendFaderOsc(Fader& f, int value, bool force) {
  if (deskLocked) {
    return;
  }

  unsigned long now = millis();

  // Only send if value changed significantly or enough time passed or force flag is set
  if (force || (abs(value - f.lastSentOscValue) >= Fconfig.sendTolerance && 
      now - f.lastOscSendTime > OSC_RATE_LIMIT)) {
    
    char oscAddress[32];
    snprintf(oscAddress, sizeof(oscAddress), "/Page%d/Fader%d", currentOSCPage, f.oscID);
    
    FADER_POS_DEBUG_PRINTF("Sent Fader %d -> %d", f.oscID, value);

    sendOscMessage(oscAddress, ",i", &value);
    
    f.lastOscSendTime = now;
    f.lastSentOscValue = value;
  }
}


// Returns the index of the fader with the given OSC ID, or -1 if not found
int getFaderIndexFromID(int id) {
  for (int i = 0; i < NUM_FADERS; i++) {
    if (faders[i].oscID == id) {
      return i;
    }
  }
  return -1;
}


void checkFaderRetry() {
  if (FaderRetryPending && millis() >= FaderRetryTime) {
    FaderRetryPending = false;
    FADER_ERROR_PRINT("Retrying fader movement...");
    moveAllFadersToSetpoints();
  }
}
