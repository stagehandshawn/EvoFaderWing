// TouchSensor.cpp

#include "TouchSensor.h"
#include "Utils.h"
#include "i2cPolling.h"

//================================
// COMMON GLOBALS
//================================

unsigned long touchDebugIntervalMs = 500;  // Minimum time between debug prints
bool irqDebugOnly = true;  // When true, only print touch debug on IRQ-triggered reads
unsigned long lastTouchDebugTime = 0;

volatile bool touchStateChanged = false;
bool touchErrorOccurred = false;
String lastTouchError = "";
int reinitializationAttempts = 0;
unsigned long lastReinitTime = 0;
const int MAX_REINIT_ATTEMPTS = 5;
const unsigned long REINIT_DELAY_BASE = 1000;  // 1 second base delay

bool touchConfirmed[NUM_FADERS] = {false};


//================================
// INTERRUPT HANDLER
//================================

void handleTouchInterrupt() {
  touchStateChanged = true;
}

//================================
// MTCH2120 IMPLEMENTATION
//================================
#if defined(TOUCH_SENSOR_MTCH2120)

// MTCH2120 tuning defaults (edit here to change sensitivity/clock/drift)
static constexpr uint8_t DEFAULT_THRESHOLD = 40;
static constexpr uint8_t DEFAULT_GAIN = 0;
static constexpr uint8_t DEFAULT_OVERSAMPLING = 3;
static constexpr uint8_t DEFAULT_MEAS_CLK = 3;                // Measurement clock (CT) lower than 3 causing some coupling 1/2 might work
static constexpr uint8_t DEFAULT_CSD = 1;
static constexpr uint8_t DEFAULT_HYSTERESIS = 1;              // Release/HYS code
static constexpr uint8_t DEFAULT_AKS = 0;                     // higher than 0 will suppress multi/long touches
static constexpr uint8_t DEFAULT_DETECTION_INTEGRATOR = 4;
static constexpr uint8_t DEFAULT_ANTI_TOUCH_INTEGRATOR = 5;
static constexpr uint8_t DEFAULT_TOUCH_DRIFT = 20;
static constexpr uint8_t DEFAULT_ANTI_TOUCH_DRIFT = 5;
static constexpr uint8_t DEFAULT_DRIFT_HOLD = 25;
static constexpr uint8_t DEFAULT_AT_RECAL_THR = 0;
static constexpr uint16_t DEFAULT_NOISE_THRESHOLD = 20;        //default 15
static constexpr uint8_t DEFAULT_NOISE_INTEGRATION = 3;
static constexpr uint8_t DEFAULT_HOP_0 = 0;
static constexpr uint8_t DEFAULT_HOP_1 = 3;
static constexpr uint8_t DEFAULT_HOP_2 = 7;
static constexpr uint16_t DEFAULT_TOUCH_PERIOD_MS = 30;
static constexpr uint16_t DEFAULT_LP_PERIOD_MS = 100;
static constexpr uint8_t DEFAULT_MAX_ON_TIME = 0;

static constexpr uint16_t DEFAULT_DEVICE_CONTROL =
    MTCH2120::DEVCTRL_FREQHOP |
    MTCH2120::DEVCTRL_WDT |
    MTCH2120::DEVCTRL_BOD |
    MTCH2120::DEVCTRL_DSP;

MTCH2120 touchSensor(Wire, MTCH2120_ADDRESS, IRQ_PIN);

static unsigned long lastTouchPollTime = 0;
static unsigned long releaseDebounceStart[NUM_FADERS] = {0};
static bool releaseDebounceActive[NUM_FADERS] = {false};

// Helper to dump current MTCH2120 configuration snapshot for debugging.
static void dumpSensorConfigSnapshot() {
  if (!touchSensor.communicating()) {
    TOUCH_ERROR_PRINT("MTCH2120 not responding");
    return;
  }

  uint8_t id = 0;
  uint8_t ver = 0;
  touchSensor.readDeviceId(id);
  touchSensor.readDeviceVersion(ver);

  MTCH2120::Status status{};
  touchSensor.readStatus(status);

  uint16_t devCtrl = 0;
  if (touchSensor.readDeviceControl(devCtrl)) {
    TOUCH_RAW_DEBUG_PRINTF("DevCtrl=0x%04X AT=%d ET=%d SAVE=%d FREQHOP=%d",
                           devCtrl,
                           (devCtrl & MTCH2120::DEVCTRL_AT) ? 1 : 0,
                           (devCtrl & MTCH2120::DEVCTRL_ET) ? 1 : 0,
                           (devCtrl & MTCH2120::DEVCTRL_SAVE) ? 1 : 0,
                           (devCtrl & MTCH2120::DEVCTRL_FREQHOP) ? 1 : 0);
  }

  MTCH2120::GroupConfig cfg{};
  if (touchSensor.readGroupConfig(cfg)) {
    TOUCH_RAW_DEBUG_PRINTF("TouchPeriod=%u LowPowerPeriod=%u DI=%u ATint=%u MaxOn=%u DHT=%u TDrift=%u ATDrift=%u ATR=%u NoiseThr=%u NoiseInt=%u Hop=%u,%u,%u",
                           cfg.touchMeasurementPeriod,
                           cfg.lowPowerMeasurementPeriod,
                           cfg.detectIntegration,
                           cfg.sensorAntiTouchIntegration,
                           cfg.sensorMaxOnTime,
                           cfg.sensorDriftHoldTime,
                           cfg.sensorTouchDriftRate,
                           cfg.sensorAntiTouchDriftRate,
                           cfg.sensorAntiTouchRecalThr,
                           cfg.noiseThreshold,
                           cfg.noiseIntegration,
                           cfg.hopFrequency[0],
                           cfg.hopFrequency[1],
                           cfg.hopFrequency[2]);
  }
}

// Apply MTCH2120 defaults to keys and group configuration.
static bool applyMtchDefaults() {
  bool ok = true;

  ok &= touchSensor.writeDeviceControl(DEFAULT_DEVICE_CONTROL);
  ok &= touchSensor.setTouchMeasurementPeriod(DEFAULT_TOUCH_PERIOD_MS);
  ok &= touchSensor.setLowPowerMeasurementPeriod(DEFAULT_LP_PERIOD_MS);
  ok &= touchSensor.setDetectIntegration(DEFAULT_DETECTION_INTEGRATOR);
  ok &= touchSensor.setAntiTouchIntegration(DEFAULT_ANTI_TOUCH_INTEGRATOR);
  ok &= touchSensor.setMaxOnTime(DEFAULT_MAX_ON_TIME);
  ok &= touchSensor.setDriftHoldTime(DEFAULT_DRIFT_HOLD);
  ok &= touchSensor.setTouchDriftRate(DEFAULT_TOUCH_DRIFT);
  ok &= touchSensor.setAntiTouchDriftRate(DEFAULT_ANTI_TOUCH_DRIFT);
  ok &= touchSensor.setAntiTouchRecalThreshold(DEFAULT_AT_RECAL_THR);
  ok &= touchSensor.setNoiseThreshold(DEFAULT_NOISE_THRESHOLD);
  ok &= touchSensor.setNoiseIntegration(DEFAULT_NOISE_INTEGRATION);
  ok &= touchSensor.setHopFrequencies(DEFAULT_HOP_0, DEFAULT_HOP_1, DEFAULT_HOP_2);

  const uint8_t threshold = (touchThreshold == 0) ? DEFAULT_THRESHOLD : touchThreshold;
  const uint8_t hysteresis = (releaseThreshold == 0) ? DEFAULT_HYSTERESIS : releaseThreshold;

  for (uint8_t key = 0; key < MTCH2120::KEY_COUNT; ++key) {
    const bool enable = key < NUM_FADERS;  // Keys 0-9 on, 10-11 off
    ok &= touchSensor.setKeyEnabled(key, enable);
    ok &= touchSensor.setThreshold(key, threshold);
    ok &= touchSensor.setGain(key, DEFAULT_GAIN);
    ok &= touchSensor.setOversampling(key, DEFAULT_OVERSAMPLING);
    ok &= touchSensor.setMeasurementClock(key, DEFAULT_MEAS_CLK);
    ok &= touchSensor.setCSD(key, DEFAULT_CSD);
    ok &= touchSensor.setHysteresis(key, hysteresis);
    ok &= touchSensor.setAKS(key, DEFAULT_AKS);
  }

  return ok;
}

// Used for debug and touch timing
void updateTouchTiming(int i, bool newTouchState) {
  unsigned long currentTime = millis();

  if (newTouchState && !faders[i].touched) {
    faders[i].touchStartTime = currentTime;
    faders[i].touchDuration = 0;
  } else if (!newTouchState && faders[i].touched) {
    faders[i].releaseTime = currentTime;
    faders[i].touchDuration = (currentTime >= faders[i].touchStartTime)
                                  ? (currentTime - faders[i].touchStartTime)
                                  : 0;
  } else if (newTouchState && faders[i].touched) {
    // Guard against millis wrap or stale start times
    if (currentTime >= faders[i].touchStartTime) {
      faders[i].touchDuration = currentTime - faders[i].touchStartTime;
    } else {
      faders[i].touchStartTime = currentTime;
      faders[i].touchDuration = 0;
    }
  }

  faders[i].touched = newTouchState;
}

bool setupTouch() {
  pinMode(IRQ_PIN, INPUT_PULLUP);

  if (!touchSensor.begin()) {
    touchErrorOccurred = true;
    lastTouchError = "MTCH2120 not found at address 0x" + String(MTCH2120_ADDRESS, HEX) + ". Check wiring!";
    return false;
  }

  touchSensor.attachChangeCallback(handleTouchInterrupt);

  if (!applyMtchDefaults()) {
    touchErrorOccurred = true;
    lastTouchError = "MTCH2120 default config write failed";
    return false;
  }

  configureAutoCalibration();

  for (int i = 0; i < NUM_FADERS; i++) {
    touchConfirmed[i] = false;
  }
  
  touchErrorOccurred = false;
  lastTouchError = "";

  // Initialization successful. Process changes right away to clear latched /CHANGE.
  processTouchChanges();
  return true;
}

static void printFaderTouchStatesInternal(unsigned long now, bool wasIrq) {
  if (!debugEnabled(DBG_CH_TOUCH_RAW, DBG_DEBUG)) {
    return;
  }

  const bool intervalHit = (now - lastTouchDebugTime >= touchDebugIntervalMs);
  const bool shouldDebug = (wasIrq || intervalHit) && (!irqDebugOnly || wasIrq);
  if (!shouldDebug) {
    return;
  }

  lastTouchDebugTime = now;
  TOUCH_RAW_DEBUG_PRINT("Raw Touch Values:");
  for (int j = 0; j < NUM_FADERS; j++) {
    MTCH2120::RawKeyData data{};
    if (touchSensor.readRawKey(j, data)) {
      int32_t delta = static_cast<int32_t>(data.reference) - static_cast<int32_t>(data.signal);
      const bool touched = touchConfirmed[j];
      const unsigned long duration = touched ? faders[j].touchDuration : 0;
      TOUCH_RAW_DEBUG_PRINTF("F%d - %s - %lums - Base: %u - Signal: %u - Delta: %ld",
                             j,
                             touched ? "TOUCHED" : "NOTOUCH",
                             duration,
                             data.reference,
                             data.signal,
                             delta);
    } else {
      TOUCH_ERROR_PRINTF("Fader %d - read error", j);
    }
  }
  dumpSensorConfigSnapshot();
}

bool processTouchChanges() {
  const unsigned long now = millis();
  bool due = touchStateChanged;
  if (TOUCH_BACKUP_POLL_MS > 0) {
    // Periodic poll: normal release path and safety net for missed IRQ
    due = due || (now - lastTouchPollTime >= TOUCH_BACKUP_POLL_MS);
  }
  if (!due) {
    return false;
  }

  bool wasIrq = touchStateChanged;

  touchStateChanged = false;
  lastTouchPollTime = now;

  uint16_t currentTouches = 0;
  bool stateUpdated = false;

  if (!touchSensor.readButtons(currentTouches)) {
    handleTouchError();
    return false;
  }

  for (int i = 0; i < NUM_FADERS; i++) {
    const bool rawTouch = bitRead(currentTouches, i);

    if (rawTouch) {
      // Clear release debounce when touched
      releaseDebounceActive[i] = false;

      if (!touchConfirmed[i]) {
        touchConfirmed[i] = true;
        updateTouchTiming(i, true);
        stateUpdated = true;
      }
      // While held, update duration
      faders[i].touchDuration = (now >= faders[i].touchStartTime)
                                    ? (now - faders[i].touchStartTime)
                                    : 0;
    } else {
    // Debounce release: require stable "not touched" for RELEASE_DEBOUNCE_MS
      if (touchConfirmed[i]) {
        if (!releaseDebounceActive[i]) {
          releaseDebounceActive[i] = true;
          releaseDebounceStart[i] = now;
        } else if (now - releaseDebounceStart[i] >= RELEASE_DEBOUNCE_MS) {
          releaseDebounceActive[i] = false;
          touchConfirmed[i] = false;
          updateTouchTiming(i, false);
          stateUpdated = true;
        }
      } else {
        releaseDebounceActive[i] = false;
      }
    }
  }

  printFaderTouchStatesInternal(now, wasIrq);

  return stateUpdated;
}

//================================
// CALIBRATION FUNCTIONS
//================================

void runTouchCalibration() {
  touchSensor.setEasyTune(true);  // Optional EasyTune pulse before calibration

  if (!applyMtchDefaults()) {
    touchErrorOccurred = true;
    lastTouchError = "MTCH2120 calibration config failed";
    return;
  }
  configureAutoCalibration();
  touchSensor.triggerCalibration();

  touchSensor.setEasyTune(false); // Optional disable after calibration
}

void manualTouchCalibration() {
  runTouchCalibration();
}

void recalibrateBaselines() {
  runTouchCalibration();
}

//================================
// AUTO-CALIBRATION FUNCTIONS
//================================

void setAutoTouchCalibration(int mode) {
  // Validate input
  if (mode < 0 || mode > 1) {
    touchErrorOccurred = true;
    lastTouchError = "Invalid auto-calibration mode. Use 0 or 1.";
    return;
  }
  
  // Update global mode variable
  autoCalibrationMode = mode;
  
  // Apply the new calibration settings
  configureAutoCalibration();
}

void configureAutoCalibration() {
  const bool enableAutoTune = autoCalibrationMode != 0;
  touchSensor.setAutoTune(enableAutoTune);
}

//================================
// ERROR HANDLING
//================================

void handleTouchError() {
  touchErrorOccurred = true;
  // Always attempt immediate reinit; keep existing I2C reset behavior
  resetI2CBus();

  if (!touchSensor.begin()) {
    lastTouchError = "MTCH2120 reinit failed";
    return;
  }

  // Reinit successful - restore settings
  touchSensor.attachChangeCallback(handleTouchInterrupt);
  runTouchCalibration();

  touchErrorOccurred = false;
  reinitializationAttempts = 0;
  lastTouchError = "Recovered from error";
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
  printFaderTouchStatesInternal(millis(), true);
}

//================================
// MPR121 IMPLEMENTATION
//================================
#elif defined(TOUCH_SENSOR_MPR121)

// MPR121 sensor object
Adafruit_MPR121 mpr121 = Adafruit_MPR121();

static unsigned long lastTouchPollTime = 0;
static unsigned long releaseDebounceStart[NUM_FADERS] = {0};
static bool releaseDebounceActive[NUM_FADERS] = {false};

// Used for debug and touch timing
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
    faders[i].touchDuration = (currentTime >= faders[i].touchStartTime)
                                  ? (currentTime - faders[i].touchStartTime)
                                  : 0;
  }
  // If continuing to be touched, update duration
  else if (newTouchState && faders[i].touched) {
    if (currentTime >= faders[i].touchStartTime) {
      faders[i].touchDuration = currentTime - faders[i].touchStartTime;
    } else {
      faders[i].touchStartTime = currentTime;
      faders[i].touchDuration = 0;
    }
  }
  
  faders[i].touched = newTouchState;
}

//================================
// MAIN SETUP FUNCTION
//================================

bool setupTouch() {
  // Configure IRQ pin as input with internal pullup resistor
  pinMode(IRQ_PIN, INPUT_PULLUP);
  
  // Start I2C communication using the shared bus settings
  resetI2CBus();
  
  // Try to initialize the MPR121 sensor
  if (!mpr121.begin(MPR121_ADDRESS)) {
    touchErrorOccurred = true;
    lastTouchError = "MPR121 not found at address 0x5A. Check wiring!";
    return false;
  }

  // Configure auto-calibration using the library's autoconfig path
  configureAutoCalibration();

  // Set global touch and release thresholds for all electrodes (after autoconfig)
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  
  // Initialize debounce and state arrays
  for (int i = 0; i < NUM_FADERS; i++) {
    releaseDebounceStart[i] = 0;
    releaseDebounceActive[i] = false;
    touchConfirmed[i] = false;
  }
  
  // Attach interrupt handler to IRQ pin
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), handleTouchInterrupt, FALLING);
  
  // Initialization successful
  return true;
}

//================================
// MAIN PROCESSING FUNCTION
//================================

bool processTouchChanges() {
  unsigned long now = millis();
  bool due = touchStateChanged;
  if (TOUCH_BACKUP_POLL_MS > 0) {
    due = due || (now - lastTouchPollTime >= TOUCH_BACKUP_POLL_MS);
  }
  if (!due) {
    return false;
  }

  bool wasIrq = touchStateChanged;
  touchStateChanged = false;
  lastTouchPollTime = now;

  uint16_t currentTouches = mpr121.touched();
  bool stateUpdated = false;

  if (debugEnabled(DBG_CH_TOUCH_RAW, DBG_DEBUG)) {
    const bool intervalHit = (now - lastTouchDebugTime >= touchDebugIntervalMs);
    const bool shouldDebug = (wasIrq || intervalHit) && (!irqDebugOnly || wasIrq);
    if (shouldDebug) {
      lastTouchDebugTime = now;
      TOUCH_RAW_DEBUG_PRINT("Raw Touch Values:");
      for (int j = 0; j < NUM_FADERS; j++) {
        uint16_t baseline = mpr121.baselineData(j);
        uint16_t filtered = mpr121.filteredData(j);
        int16_t delta = baseline - filtered;
        TOUCH_RAW_DEBUG_PRINTF("Fader %d - Base: %u, Filtered: %u, Delta: %d", j, baseline, filtered, delta);
      }
    }
  }

  if (currentTouches == 0xFFFF) {
    handleTouchError();
    return false;
  }

  for (int i = 0; i < NUM_FADERS; i++) {
    bool rawTouch = bitRead(currentTouches, i);

    if (rawTouch) {
      releaseDebounceActive[i] = false;
      if (!touchConfirmed[i]) {
        touchConfirmed[i] = true;
        updateTouchTiming(i, true);
        stateUpdated = true;
      }
      if (touchConfirmed[i]) {
        faders[i].touchDuration = (now >= faders[i].touchStartTime)
                                      ? (now - faders[i].touchStartTime)
                                      : 0;
      }
    } else {
      if (touchConfirmed[i]) {
        if (!releaseDebounceActive[i]) {
          releaseDebounceActive[i] = true;
          releaseDebounceStart[i] = now;
        } else if (now - releaseDebounceStart[i] >= RELEASE_DEBOUNCE_MS) {
          releaseDebounceActive[i] = false;
          touchConfirmed[i] = false;
          updateTouchTiming(i, false);
          stateUpdated = true;
        }
      } else {
        releaseDebounceActive[i] = false;
      }
    }

  }

  return stateUpdated;
}

//================================
// CALIBRATION FUNCTIONS
//================================

static void applyAutoconfig(bool enable) {
  if (enable) {
    // Enable Adafruit's autoconfig with standard limits for 3.3V
    mpr121.writeRegister(MPR121_AUTOCONFIG0, 0x0B);
    mpr121.writeRegister(MPR121_UPLIMIT, 200);
    mpr121.writeRegister(MPR121_TARGETLIMIT, 180);
    mpr121.writeRegister(MPR121_LOWLIMIT, 130);
  } else {
    // Disable autoconfig
    mpr121.writeRegister(MPR121_AUTOCONFIG0, 0x00);
  }
}

static void recalibrateBaselines();

void manualTouchCalibration() {
  recalibrateBaselines();
}

void runTouchCalibration() {
  recalibrateBaselines();
}

static void recalibrateBaselines() {
  configureAutoCalibration();
  mpr121.setThresholds(touchThreshold, releaseThreshold);
}

//================================
// AUTO-CALIBRATION FUNCTIONS
//================================

void setAutoTouchCalibration(int mode) {
  // Validate input
  if (mode < 0 || mode > 1) {
    touchErrorOccurred = true;
    lastTouchError = "Invalid auto-calibration mode. Use 0 or 1.";
    return;
  }
  
  // Update global mode variable
  autoCalibrationMode = mode;
  
  // Apply the new calibration settings
  configureAutoCalibration();
}

void configureAutoCalibration() {
  // Toggle autoconfig based on mode: 0 = off, 1/2 = on
  mpr121.writeRegister(MPR121_ECR, 0x00);  // Stop electrodes while reconfiguring
  bool enableAutoconfig = autoCalibrationMode != 0;
  applyAutoconfig(enableAutoconfig);
  mpr121.writeRegister(MPR121_ECR, 0x8C);  // Enable 12 electrodes (0x80 + 12)
}

//================================
// ERROR HANDLING
//================================

void handleTouchError() {
  touchErrorOccurred = true;
  // Always attempt immediate reinit; keep existing I2C reset behavior
  resetI2CBus();

  if (!mpr121.begin(MPR121_ADDRESS)) {
    lastTouchError = "MPR121 reinit failed";
    return;
  }

  // Reinit successful - restore settings
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  configureAutoCalibration();

  touchErrorOccurred = false;
  reinitializationAttempts = 0;
  lastTouchError = "Recovered from error";
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
  if (!debugEnabled(DBG_CH_TOUCH_CORE, DBG_DEBUG)) {
    return;
  }
  TOUCH_DEBUG_PRINT("Fader Touch States:");
  for (int i = 0; i < NUM_FADERS; i++) {
    if (faders[i].touched) {
      TOUCH_DEBUG_PRINTF("  Fader %d: TOUCHED (%lums)", i, faders[i].touchDuration);
    } else {
      TOUCH_DEBUG_PRINTF("  Fader %d: released", i);
    }
  }
}

#else
#error "No touch sensor selected"
#endif
