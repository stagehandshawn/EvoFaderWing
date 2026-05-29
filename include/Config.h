// Config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

//================================
// IDENTITY
//================================
#define PROJECT_NAME "EvoFaderWing"
#define SW_VERSION      "0.4"

//================================
// DEBUG CHANNELS
//================================
enum DebugLevel : uint8_t {
  DBG_OFF = 0,
  DBG_ERROR = 1,
  DBG_DEBUG = 2
};

enum DebugChannel : uint8_t {
  DBG_CH_SYSTEM = 0,
  DBG_CH_WEB,
  DBG_CH_NETWORK,
  DBG_CH_OSC,
  DBG_CH_I2C_BUS,
  DBG_CH_FADER_CORE,
  DBG_CH_FADER_POSITION,
  DBG_CH_TOUCH_CORE,
  DBG_CH_TOUCH_RAW,
  DBG_CH_CALIBRATION,
  DBG_CH_EEPROM,
  DBG_CH_LED_EXEC,
  DBG_CH_OLED,
  DBG_CH_COUNT
};

static const uint8_t DEBUG_CHANNEL_COUNT = (uint8_t)DBG_CH_COUNT;
static const uint8_t DEBUG_CONFIG_VERSION = 2;

//================================
// HARDWARE CONFIGURATION
//================================

// Fader configuration
#define NUM_FADERS      10       // Total number of motorized faders
#define SERIAL_BAUD     115200   // Baud rate for USB serial output/debug

// Motor control defaults tuned for a 12V PSU; adjust if the hardware uses a lower supply voltage.
#define MAX_PWM        150      // Default motor speed (PWM duty cycle) during normal operation (0–255) BEST at 100-150
#define CALIB_PWM       80       // Reduced motor speed during auto-calibration phase
#define MIN_PWM         40       // Minimum PWM to overcome motor inertia 
#define PWM_FREQ     25000      // Frequency of the motors PWM output 25khz
#define FADER_MOVE_TIMEOUT     2000   // Time in MS a fader must not be moving before force stopped
#define RETRY_INTERVAL         1000    // How long before trying to move a stuck fader
#define FADER_MAX_FAILURES       3     // Consecutive timeouts before disabling a fader motor

// Fader position tolerances
#define TARGET_TOLERANCE 1       // OSC VALUE How close the fader must be to setpoint to consider "done"
#define SEND_TOLERANCE   2       // Amout of change in OSC (0-100) before senind an osc update
#define ANALOG_NOISE_TOLERANCE 1 // Suppress tiny ADC jitter (counts) before mapping to OSC (current default set for 8bit reads)
#define SLOW_ZONE        25      // OSC units - start slowing earlier for smoother approach
#define FAST_ZONE        60      // OSC units - when to use full speed

// Calibration settings
#define PLATEAU_THRESH   2       // Threshold (analog delta) to consider that the fader has stopped moving
#define PLATEAU_COUNT    10      // How many stable readings in a row needed to "lock in" max or min during calibration


// OSC settings
#define OSC_VALUE_THRESHOLD 2    // Minimum value change to send OSC update
#define OSC_RATE_LIMIT     20    // Minimum ms between OSC messages

// NeoPixel configuration
#define NEOPIXEL_PIN 12
#define PIXELS_PER_FADER 24
#define NUM_PIXELS (NUM_FADERS * PIXELS_PER_FADER)

// Executor key NeoPixel strip (40 keys, 2 pixels each)
#define NUM_EXECUTORS_TRACKED 40
#define EXECUTOR_PIXELS_PER_KEY 2
#define EXECUTOR_LED_PIN 53   // Teensy 4.1 pin for the key strip
#define EXECUTOR_LED_COUNT (NUM_EXECUTORS_TRACKED * EXECUTOR_PIXELS_PER_KEY)
#define EXECUTOR_BASE_BRIGHTNESS 10   // Default base brightness for populated-but-off keys
#define EXECUTOR_ACTIVE_BRIGHTNESS 80 // Default brightness for active/on keys (unpopulated stays dark)

// Touch sensor configuration
#if defined(TOUCH_SENSOR_MTCH2120) && defined(TOUCH_SENSOR_MPR121)
#error "Select only one touch sensor: TOUCH_SENSOR_MTCH2120 or TOUCH_SENSOR_MPR121"
#endif

// Default to MTCH2120
#if !defined(TOUCH_SENSOR_MTCH2120) && !defined(TOUCH_SENSOR_MPR121)
#define TOUCH_SENSOR_MTCH2120 1
#endif

#define IRQ_PIN 13

#ifndef MTCH2120_ADDRESS
#define MTCH2120_ADDRESS 0x20
#endif

#ifndef MPR121_ADDRESS
#define MPR121_ADDRESS 0x5A
#endif

//================================
// PIN ASSIGNMENTS
//================================

// Analog input pins for fader position
extern const uint8_t ANALOG_PINS[NUM_FADERS];

// PWM output pins for motor speed
extern const uint8_t PWM_PINS[NUM_FADERS];

// Direction control pins for motors
extern const uint8_t DIR_PINS1[NUM_FADERS];
extern const uint8_t DIR_PINS2[NUM_FADERS];

// OSC IDs for each fader
extern const uint16_t OSC_IDS[NUM_FADERS];

//================================
// BRIGHTNESS SETTINGS
//================================


//================================
// NETWORK CONFIGURATION
//================================
constexpr uint32_t kDHCPTimeout = 15000;  // Timeout for DHCP in milliseconds
constexpr uint16_t kOSCPort = 8000;       // Default OSC port
constexpr char kServiceName[] = "evofaderwing"; // mDNS service name and Hostname

// Network configuration structure
struct NetworkConfig {
  IPAddress staticIP;     // Local static IP address
  IPAddress gateway;      // Default network gateway
  IPAddress subnet;       // Subnet mask
  IPAddress sendToIP;     // OSC destination IP address
  uint16_t  oscPort;      // Shared OSC send/receive port (e.g. 8000)
  bool      useDHCP;      // If true, use DHCP instead of static IP
};

//================================
// FADER CONFIGURATION
//================================

// Configuration structure that can be saved to EEPROM
struct FaderConfig {
  uint8_t minPwm;
  uint8_t maxPwm;
  uint8_t calibratePwm;
  uint8_t targetTolerance;
  uint8_t sendTolerance;
  uint8_t slowZone;
  uint8_t fastZone;
  uint8_t baseBrightness;         // Default idle brightness
  uint8_t touchedBrightness;      // Brightness when fader is touched
  unsigned long fadeTime;         // Fade duration in milliseconds
  bool serialDebug;
  bool sendKeystrokes;       // Send keystroke using usb rather than osc for exec keys, this gives more native support (can store using exec key directly)
  bool useLevelPixels;       // When true, render per-fader level bars instead of full fill
  bool allowFaderOscWithoutTouch;  // Allow local/manual fader OSC send even without touch detect
  uint8_t debugConfigVersion;       // Version tag for persisted debug channel layout
  uint8_t debugLevel[DEBUG_CHANNEL_COUNT];  // Per-channel debug verbosity
};

// Executor LED configuration
struct ExecConfig {
  uint8_t baseBrightness;     // Brightness when executor is populated but off
  uint8_t activeBrightness;   // Brightness when executor is on/active
  bool useStaticColor;        // When true, use static RGB color instead of white
  uint8_t staticRed;          // Static color components (0-255)
  uint8_t staticGreen;
  uint8_t staticBlue;
  uint8_t reserved[2];        // Space for future options
};

// Touch sensor configuration
struct TouchConfig {
  uint8_t autoCalibrationMode;  // 0=AutoTune off, 1=AutoTune on
  uint8_t touchThreshold;       // Per-key threshold (higher = less sensitive)
  uint8_t releaseThreshold;     // MTCH: hysteresis code 0-7, MPR121: release threshold 1-255
  uint8_t reserved[5];          // Reserved space for future touch parameters (unused for now)
};

//================================
// FADER STRUCT
//================================
struct Fader {
  uint8_t analogPin;        // Analog input from fader wiper
  uint8_t pwmPin;           // PWM output to motor driver
  uint8_t dirPin1;          // Motor direction pin 1
  uint8_t dirPin2;          // Motor direction pin 2

  int minVal;               // Calibrated analog min
  int maxVal;               // Calibrated analog max

  uint8_t setpoint;          // Target position
  bool motorEnabled;        // Motor state (can be disabled after repeated failures)
  uint8_t failureCount;     // Consecutive failures to reach target
  unsigned long lastFailureTime; // Timestamp of last failure

  uint8_t lastReportedValue;    // Last value printed or sent
  uint8_t lastSentOscValue;     // Last value sent via OSC

  unsigned long lastOscSendTime; // Time of last OSC message

  uint16_t oscID;           // OSC ID like 201 for /Page2/Fader201
  int lastAnalogValue;      // Last raw analog reading to suppress small jitter
  int lastSampledOscValue;  // Last sampled OSC value used for local-manual movement detection
  bool manualOverride;      // Local/manual movement currently owns this fader
  unsigned long manualLastMoveTime; // Last time local/manual movement was detected
  unsigned long remoteControlLockoutUntil; // Prevent motor-driven movement from being treated as manual right after OSC input


  // Color variables
  uint8_t red;           // Red component (0-255)
  uint8_t green;         // Green component (0-255)
  uint8_t blue;          // Blue component (0-255)

  // NeoPixel brightness fading
  uint8_t currentBrightness;           // Actual brightness applied this frame
  uint8_t targetBrightness;            // Target brightness based on touch
  unsigned long brightnessStartTime;   // When fade began
  uint8_t lastReportedBrightness;      // For debug: last brightness sent
  uint32_t lastRenderedColor;          // Last color pushed to strip (scaled)
  uint8_t lastRenderedSetpoint;        // Last setpoint used when rendering level pixels
  
  // Touch Values
  bool touched;                 // Fader is touched or not
  unsigned long touchStartTime; // When the fader was touched
  unsigned long touchDuration;  // How long the fader has been touched
  unsigned long releaseTime;    // When the fader was last released
};

//================================
// GLOBAL VARIABLES DECLARATIONS
//================================

// Main fader array
extern Fader faders[NUM_FADERS];

// Configuration instances
extern NetworkConfig netConfig;
extern FaderConfig Fconfig;
extern ExecConfig execConfig;

// Page tracking
extern int currentOSCPage;

// Wing status flags (bitmask sent via /wingStatus OSC message)
#define WING_STATUS_DESK_LOCK     1  // Bit 0: desk is locked
#define WING_STATUS_CMD_MODE      2  // Bit 1: MA3 cmd line active — add executor to cmdline
#define WING_STATUS_CMD_EXEC_MODE 4  // Bit 2: MA3 cmd line active — add executor + auto-execute
#define WING_STATUS_CMD_COPY_SRC  8  // Bit 3: copy/move with source already selected — target determines + prefix
extern bool deskLocked;

// Touch sensor globals
extern int autoCalibrationMode;
extern uint8_t touchThreshold;
extern uint8_t releaseThreshold;

// Network reset check
extern bool checkForReset;
extern unsigned long resetCheckStartTime;

// Calibration state flag
extern bool calibrationInProgress;

void displayIPAddress();
void displayShowResetHeader();

// Debug setting
extern bool debugMode;

void loadDefaultDebugLevels(uint8_t* levels, size_t count);
const char* debugChannelName(DebugChannel channel);

#endif // CONFIG_H
