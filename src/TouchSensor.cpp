#include "TouchSensor.h"
#include "Utils.h"

//================================
// GLOBAL VARIABLES DEFINITIONS
//================================


// MPR121 sensor object
Adafruit_MPR121 mpr121 = Adafruit_MPR121();

// Optional debug flag for printing raw touch data
bool touchDebug = false;
unsigned long touchDebugIntervalMs = 500;  // Minimum time between debug prints
unsigned long lastTouchDebugTime = 0;

// Interrupt and error handling
volatile bool touchStateChanged = false;
bool touchErrorOccurred = false;
String lastTouchError = "";
int reinitializationAttempts = 0;
unsigned long lastReinitTime = 0;
const int MAX_REINIT_ATTEMPTS = 5;
const unsigned long REINIT_DELAY_BASE = 1000;  // 1 second base delay

// Debounce arrays
unsigned long debounceStart[NUM_FADERS] = {0};
bool touchConfirmed[NUM_FADERS] = {false};

//================================
// INTERRUPT HANDLER
//================================

void handleTouchInterrupt() {
  touchStateChanged = true;
}

//================================
// TOUCH TIMING FUNCTIONS
//================================

void updateTouchTiming(int i, bool newTouchState) {
  unsigned long currentTime = millis();
  
  // If state changed from released to touched
  if (newTouchState && !faders[i].touched) {
    faders[i].touchStartTime = currentTime;
    faders[i].touchDuration = 0;
  }
  // If state changed from touched to released
  else if (!newTouchState && faders[i].touched) {
    faders[i].releaseTime = currentTime;
    // Calculate how long it was touched
    faders[i].touchDuration = currentTime - faders[i].touchStartTime;
  }
  // If continuing to be touched, update duration
  else if (newTouchState && faders[i].touched) {
    faders[i].touchDuration = currentTime - faders[i].touchStartTime;
  }
  
  faders[i].touched = newTouchState;
}

//================================
// MAIN SETUP FUNCTION
//================================

bool setupTouch() {
  // Configure IRQ pin as input with internal pullup resistor
  pinMode(IRQ_PIN, INPUT_PULLUP);
  
  // Start I2C communication
  Wire.begin();
  
  // Try to initialize the MPR121 sensor
  if (!mpr121.begin(MPR121_ADDRESS)) {
    touchErrorOccurred = true;
    lastTouchError = "MPR121 not found at address 0x5A. Check wiring!";
    return false;
  }
  
  // Set global touch and release thresholds for all electrodes
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  
  // Initialize debounce and state arrays
  for (int i = 0; i < NUM_FADERS; i++) {
    debounceStart[i] = 0;
    touchConfirmed[i] = false;
  }
  
  // Configure auto-calibration to conservative mode (default)
  configureAutoCalibration();
  
  // Attach interrupt handler to IRQ pin
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), handleTouchInterrupt, FALLING);
  
  // Initialization successful
  return true;
}

//================================
// MAIN PROCESSING FUNCTION
//================================

bool processTouchChanges() {
  //static uint16_t lastRawTouchBits = 0;
  uint16_t currentTouches = mpr121.touched();
  unsigned long now = millis();
  bool stateUpdated = false;

  if (touchDebug && now - lastTouchDebugTime >= touchDebugIntervalMs) {
    lastTouchDebugTime = now;
    debugPrint("Raw Touch Values:");
    for (int j = 0; j < NUM_FADERS; j++) {
      uint16_t baseline = mpr121.baselineData(j);
      uint16_t filtered = mpr121.filteredData(j);
      int16_t delta = baseline - filtered;
      debugPrintf("Fader %d - Base: %u, Filtered: %u, Delta: %d", j, baseline, filtered, delta);
    }
  }

  if (currentTouches == 0xFFFF) {
    handleTouchError();
    return false;
  }

  for (int i = 0; i < NUM_FADERS; i++) {
    bool rawTouch = bitRead(currentTouches, i);

    // === TOUCH DETECTED ===
    if (rawTouch && !touchConfirmed[i]) {
      // Start debounce timer if just now touched
      if (debounceStart[i] == 0) {
        debounceStart[i] = now;
      }
      // Confirm after debounce time
      else if (now - debounceStart[i] >= TOUCH_CONFIRM_MS) {
        touchConfirmed[i] = true;
        debounceStart[i] = 0;
        updateTouchTiming(i, true);
        stateUpdated = true;
      }
    }

    // === RELEASE DETECTED ===
    else if (!rawTouch && touchConfirmed[i]) {
      // Start debounce timer if just now released
      if (debounceStart[i] == 0) {
        debounceStart[i] = now;
      }
      // Confirm release after debounce time
      else if (now - debounceStart[i] >= RELEASE_CONFIRM_MS) {
        touchConfirmed[i] = false;
        debounceStart[i] = 0;
        updateTouchTiming(i, false);
        stateUpdated = true;
      }
    }

    // === NO CHANGE ===
    else {
      debounceStart[i] = 0;  // Reset if bouncing
    }

    // While held, update duration
    if (touchConfirmed[i]) {
      faders[i].touchDuration = now - faders[i].touchStartTime;
    }
  }

  return stateUpdated;
}

//================================
// CALIBRATION FUNCTIONS
//================================

void manualTouchCalibration() {
  // Set touch and release thresholds for all electrodes
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  
  // Recalibrate baseline values (what "no touch" looks like)
  recalibrateBaselines();
}

void recalibrateBaselines() {
  // Stop the sensor temporarily
  mpr121.writeRegister(0x5E, 0x00);
  delay(10);
  
  // Trigger a full reset with auto-configuration
  mpr121.writeRegister(0x80, 0x63);
  delay(10);
  
  // Resume normal operation
  // Enable all 12 electrodes (0x8F = binary 10001111)
  mpr121.writeRegister(0x5E, 0x8F);
  
  // Reapply current auto-calibration settings
  configureAutoCalibration();
}

//================================
// AUTO-CALIBRATION FUNCTIONS
//================================

void setAutoTouchCalibration(int mode) {
  // Validate input
  if (mode < 0 || mode > 2) {
    touchErrorOccurred = true;
    lastTouchError = "Invalid auto-calibration mode. Use 0-2.";
    return;
  }
  
  // Update global mode variable
  autoCalibrationMode = mode;
  
  // Apply the new calibration settings
  configureAutoCalibration();
}

void configureAutoCalibration() {
  switch (autoCalibrationMode) {
    case 0: // Disabled - baselines never change
      // Set all parameters to 0xFF to disable auto-calibration
      mpr121.writeRegister(MPR121_MHDR, 0xFF);
      mpr121.writeRegister(MPR121_NHDR, 0xFF);
      mpr121.writeRegister(MPR121_NCLR, 0xFF);
      mpr121.writeRegister(MPR121_FDLR, 0xFF);
      mpr121.writeRegister(MPR121_MHDF, 0xFF);
      mpr121.writeRegister(MPR121_NHDF, 0xFF);
      mpr121.writeRegister(MPR121_NCLF, 0xFF);
      mpr121.writeRegister(MPR121_FDLF, 0xFF);
      mpr121.writeRegister(MPR121_NHDT, 0xFF);
      mpr121.writeRegister(MPR121_NCLT, 0xFF);
      mpr121.writeRegister(MPR121_FDLT, 0xFF);
      break;
      
    case 1: // Normal - standard auto-calibration
      // Use manufacturer recommended values for normal operation
      mpr121.writeRegister(MPR121_MHDR, 0x01);
      mpr121.writeRegister(MPR121_NHDR, 0x01);
      mpr121.writeRegister(MPR121_NCLR, 0x0E);
      mpr121.writeRegister(MPR121_FDLR, 0x00);
      mpr121.writeRegister(MPR121_MHDF, 0x01);
      mpr121.writeRegister(MPR121_NHDF, 0x05);
      mpr121.writeRegister(MPR121_NCLF, 0x01);
      mpr121.writeRegister(MPR121_FDLF, 0x00);
      mpr121.writeRegister(MPR121_NHDT, 0x00);
      mpr121.writeRegister(MPR121_NCLT, 0x00);
      mpr121.writeRegister(MPR121_FDLT, 0x00);
      break;
      
    case 2: // Conservative - slow adaptation (DEFAULT)
      // Settings that only adapt to real environmental changes
      mpr121.writeRegister(MPR121_MHDR, 0x01);
      mpr121.writeRegister(MPR121_NHDR, 0x01);
      mpr121.writeRegister(MPR121_NCLR, 0x1C);
      mpr121.writeRegister(MPR121_FDLR, 0x08);
      mpr121.writeRegister(MPR121_MHDF, 0x01);
      mpr121.writeRegister(MPR121_NHDF, 0x01);
      mpr121.writeRegister(MPR121_NCLF, 0x1C);
      mpr121.writeRegister(MPR121_FDLF, 0x08);
      mpr121.writeRegister(MPR121_NHDT, 0x01);
      mpr121.writeRegister(MPR121_NCLT, 0x10);
      mpr121.writeRegister(MPR121_FDLT, 0x20);
      break;
  }
}

//================================
// ERROR HANDLING
//================================

void handleTouchError() {
  touchErrorOccurred = true;
  
  // Calculate exponential backoff delay
  unsigned long currentTime = millis();
  unsigned long timeSinceLastReinit = currentTime - lastReinitTime;
  unsigned long requiredDelay = REINIT_DELAY_BASE * (1 << reinitializationAttempts);
  
  // If we haven't waited long enough since last attempt, exit
  if (timeSinceLastReinit < requiredDelay && reinitializationAttempts > 0) {
    return;
  }
  
  // Check if we've exceeded maximum attempts
  if (reinitializationAttempts >= MAX_REINIT_ATTEMPTS) {
    lastTouchError = "MPR121 failed after " + String(MAX_REINIT_ATTEMPTS) + " reinit attempts";
    return;
  }
  
  // Increment counter and record time
  reinitializationAttempts++;
  lastReinitTime = currentTime;
  
  // Try to reinitialize with more robust sequence
  Wire.end();
  delay(50);
  Wire.begin();
  delay(50);
  
  if (!mpr121.begin(MPR121_ADDRESS)) {
    lastTouchError = "MPR121 reinit failed (attempt " + String(reinitializationAttempts) + ")";
    return;
  }
  
  // Reinit successful - restore settings
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  configureAutoCalibration();
  
  // Clear error only if we were successful
  touchErrorOccurred = false;
  lastTouchError = "Recovered from error after " + String(reinitializationAttempts) + " attempts";
}

String getLastTouchError() {
  return lastTouchError;
}

bool hasTouchError() {
  return touchErrorOccurred;
}

void clearTouchError() {
  touchErrorOccurred = false;
  lastTouchError = "";
  reinitializationAttempts = 0;
}

//================================
// UTILITY FUNCTIONS
//================================

void printFaderTouchStates() {
  if (!touchDebug){
    return;
  }
  debugPrint("Fader Touch States:");
  for (int i = 0; i < NUM_FADERS; i++) {
    if (faders[i].touched) {
      debugPrintf("  Fader %d: TOUCHED (%lums)", i, faders[i].touchDuration);
    } else {
      debugPrintf("  Fader %d: released", i);
    }
  }
}