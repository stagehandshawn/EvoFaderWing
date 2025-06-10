#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>
#include <PID_v1.h>

//================================
// HARDWARE CONFIGURATION
//================================

// Fader configuration
#define NUM_FADERS      10       // Total number of motorized faders
#define SERIAL_BAUD     115200   // Baud rate for USB serial output/debug

// Motor control settings
#define DEFAULT_PWM     180      // Default motor speed (PWM duty cycle) during normal operation (0–255)
#define CALIB_PWM       100      // Reduced motor speed during auto-calibration phase
#define MOTOR_DEADZONE  30       // Minimum PWM to overcome motor inertia

// PID controller settings
#define PID_KP          0.5      // PID proportional gain
#define PID_KI          0.05     // PID integral gain
#define PID_KD          0.1      // PID derivative gain
#define PID_SAMPLE_TIME 25       // How often (in milliseconds) PID is evaluated

// Fader position tolerances
#define TARGET_TOLERANCE 15      // How close (in analog units) fader must be to setpoint to consider "done"
#define SEND_TOLERANCE   8       // Minimum analog change before reporting fader movement default is 1024/127 to give one logical step

// Calibration settings
#define PLATEAU_THRESH   2       // Threshold (analog delta) to consider that the fader has stopped moving
#define PLATEAU_COUNT    10      // How many stable readings in a row needed to "lock in" max or min during calibration

// Filtering and smoothing
#define FILTER_SIZE     5        // Size of moving average filter for smoothing readings
#define MAX_VELOCITY_CHANGE 5.0  // Maximum change in PWM per update for smooth acceleration

// OSC settings
#define OSC_VALUE_THRESHOLD 2    // Minimum value change to send OSC update
#define OSC_RATE_LIMIT     20    // Minimum ms between OSC messages

// NeoPixel configuration
#define NEOPIXEL_PIN 12
#define PIXELS_PER_FADER 24
#define NUM_PIXELS (NUM_FADERS * PIXELS_PER_FADER)

// Touch sensor configuration
#define IRQ_PIN 13
#define MPR121_ADDRESS 0x5A

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
extern uint8_t baseBrightness;         // Default idle brightness
extern uint8_t touchedBrightness;      // Brightness when fader is touched
extern unsigned long fadeTime;         // Fade duration in milliseconds

//================================
// STATE MACHINE DEFINITIONS
//================================
enum FaderState {
  FADER_IDLE,         // Fader is not moving and at target position
  FADER_MOVING,       // Fader is actively moving toward target
  FADER_CALIBRATING,  // Fader is in calibration mode
  FADER_ERROR         // Fader has encountered an error
};

//================================
// NETWORK CONFIGURATION
//================================
constexpr uint32_t kDHCPTimeout = 15000;  // Timeout for DHCP in milliseconds
constexpr uint16_t kOSCPort = 8000;       // Default OSC port
constexpr char kServiceName[] = "gma3-faderwing"; // mDNS service name

// Network configuration structure
struct NetworkConfig {
  IPAddress staticIP;     // Local static IP address
  IPAddress gateway;      // Default network gateway
  IPAddress subnet;       // Subnet mask
  IPAddress sendToIP;     // OSC destination IP address
  uint16_t  receivePort;  // OSC listening port (e.g. 8000)
  uint16_t  sendPort;     // OSC destination port (e.g. 9000)
  bool      useDHCP;      // If true, use DHCP instead of static IP
};

//================================
// FADER CONFIGURATION
//================================

// Configuration structure that can be saved to EEPROM
struct FaderConfig {
  float pidKp;
  float pidKi;
  float pidKd;
  uint8_t motorDeadzone;
  uint8_t defaultPwm;
  uint8_t calibratePwm;
  uint8_t targetTolerance;
  uint8_t sendTolerance;
  bool invertMotorDirection;
  bool invertFaderRange;
};

// Touch sensor configuration
struct TouchConfig {
  uint8_t autoCalibrationMode;  // 0=disabled, 1=normal, 2=conservative (default)
  uint8_t touchThreshold;       // Default 12, higher = less sensitive
  uint8_t releaseThreshold;     // Default 6, lower = harder to release
  uint8_t reserved[5];          // Reserved space for future touch parameters
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

  double setpoint;          // Target position
  double current;           // Current analog reading
  double smoothedPosition;  // Filtered position
  double motorOutput;       // PID output
  double lastMotorOutput;   // Last motor output for velocity limiting

  PID* pidController;       // Pointer to PID controller
  FaderState state;         // Current state in state machine
  int lastReportedValue;    // Last value printed or sent
  unsigned long lastMoveTime; // Time of last movement
  unsigned long lastOscSendTime; // Time of last OSC message
  int lastSentOscValue;     // Last value sent via OSC
  bool suppressOSCOut;     // Suppress OSC out or Not
  uint16_t oscID;           // OSC ID like 201 for /Page2/Fader201
  
  // Filter variables
  int readings[FILTER_SIZE];
  int readIndex;
  int readingsTotal;

  // Color variables
  uint8_t red;           // Red component (0-255)
  uint8_t green;         // Green component (0-255)
  uint8_t blue;          // Blue component (0-255)
  bool colorUpdated;     // Flag to indicate new color data received

  // NeoPixel brightness fading
  uint8_t currentBrightness;           // Actual brightness applied this frame
  uint8_t targetBrightness;            // Target brightness based on touch
  unsigned long brightnessStartTime;   // When fade began
  uint8_t lastReportedBrightness;      // For debug: last brightness sent
  
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

// Page tracking
extern int currentOSCPage;

// Touch sensor globals
extern int autoCalibrationMode;
extern uint8_t touchThreshold;
extern uint8_t releaseThreshold;

// Network reset check
extern bool checkForReset;
extern unsigned long resetCheckStartTime;

void displayIPAddress();

// Debug setting
extern bool debugMode;

#endif // CONFIG_H