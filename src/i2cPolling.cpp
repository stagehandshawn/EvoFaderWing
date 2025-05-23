// === TEENSY 4.1 I2C POLLING MASTER ===
// Polls 5 ATmega slaves for keyboard and encoder data
// No interrupt pins needed - pure polling approach
// Enhanced with encoder button press/release handling

#include <Wire.h>
#include "i2cPolling.h"

// === I2C Slave Addresses ===
#define I2C_ADDR_KEYBOARD  0x10  // Keyboard matrix ATmega
#define I2C_ADDR_ENCODER1  0x11  // First encoder ATmega (5 encoders)
#define I2C_ADDR_ENCODER2  0x12  // Second encoder ATmega (5 encoders)  
#define I2C_ADDR_ENCODER3  0x13  // Third encoder ATmega (5 encoders)
#define I2C_ADDR_ENCODER4  0x14  // Fourth encoder ATmega (5 encoders)

// Array of all slave addresses for easy iteration
const uint8_t slaveAddresses[] = {
  I2C_ADDR_KEYBOARD,
  I2C_ADDR_ENCODER1, 
  I2C_ADDR_ENCODER2,
  I2C_ADDR_ENCODER3,
  I2C_ADDR_ENCODER4
};
const int numSlaves = sizeof(slaveAddresses) / sizeof(slaveAddresses[0]);

// === Protocol Constants ===
#define DATA_TYPE_ENCODER  0x01  // Data type identifier for encoder messages
#define DATA_TYPE_KEYPRESS 0x02  // Data type identifier for keypress messages
#define DATA_TYPE_BUTTON   0x03  // Data type identifier for encoder button press

// === Button State Constants ===
#define BUTTON_PRESSED  1    // Button state: pressed down
#define BUTTON_RELEASED 0    // Button state: released/not pressed

// === Timing Control ===
unsigned long lastPollTime = 0;     // Time of last poll cycle
const unsigned long pollInterval = 1; // Poll every 1ms (1000Hz)

void setupI2cPolling() {
  // Initialize I2C as master with fast speed
  Wire.begin();                // Start I2C as master
  Wire.setClock(400000);       // Set to 400kHz (fast mode)
  
  // Initialize serial for debugging
  Serial.begin(115200);        // High baud rate for fast debug output
  Serial.println("[MASTER] Teensy I2C polling master started");
  Serial.print("Polling ");
  Serial.print(numSlaves);
  Serial.println(" slaves...");
  
  // List all slave addresses being polled
  for (int i = 0; i < numSlaves; i++) {
    Serial.print("  Slave ");
    Serial.print(i);
    Serial.print(": 0x");
    Serial.println(slaveAddresses[i], HEX);
  }
  
  Serial.println("[INFO] Enhanced with encoder button press/release handling");
}

void handleI2c() {
  unsigned long currentTime = millis();
  
  // Check if it's time for next poll cycle
  if (currentTime - lastPollTime >= pollInterval) {
    lastPollTime = currentTime;
    
    // Poll each slave device for data
    for (int i = 0; i < numSlaves; i++) {
      pollSlave(slaveAddresses[i], i);
    }
  }
  
  // Do other work here (OSC sending, processing, etc.)
  // Since polling only takes ~2.5ms, plenty of time for other tasks
}

void pollSlave(uint8_t address, int slaveIndex) {
  // Request data from the specified slave
  Wire.requestFrom(address, static_cast<uint8_t>(32));  // Request up to 32 bytes
  
  // Check if slave responded
  if (Wire.available() < 2) {
    // Need at least 2 bytes (type + count)
    return;
  }
  
  // Read protocol header
  uint8_t dataType = Wire.read();  // First byte: data type
  uint8_t count = Wire.read();     // Second byte: number of items
  
  // Process based on data type
  switch (dataType) {
    case DATA_TYPE_ENCODER:
      processEncoderData(count, slaveIndex, address);
      break;
      
    case DATA_TYPE_KEYPRESS:
      processKeypressData(count, slaveIndex, address);
      break;
      
    case DATA_TYPE_BUTTON:
      // NEW: Process encoder button press/release events
      processButtonData(count, slaveIndex, address);
      break;
      
    default:
      // Unknown data type, skip remaining bytes
      while (Wire.available()) Wire.read();
      Serial.print("[ERROR] Unknown data type 0x");
      Serial.print(dataType, HEX);
      Serial.print(" from slave 0x");
      Serial.println(address, HEX);
      break;
  }
}

void processEncoderData(uint8_t count, int slaveIndex, uint8_t address) {
  if (count == 0) return; // No encoder data
  
  Serial.print("[ENC] Slave 0x");
  Serial.print(address, HEX);
  Serial.print(" has ");
  Serial.print(count);
  Serial.println(" encoder events:");
  
  // Process each encoder event (2 bytes each)
  for (int i = 0; i < count && Wire.available() >= 2; i++) {
    uint8_t encoderWithDir = Wire.read();  // Encoder index with direction bit
    uint8_t velocity = Wire.read();        // Velocity value
    
    // Extract encoder number and direction
    uint8_t encoderNumber = encoderWithDir & 0x7F;  // Lower 7 bits
    bool isPositive = (encoderWithDir & 0x80) != 0; // MSB = direction
    
    // Debug output
    Serial.print("  Encoder ");
    Serial.print(encoderNumber);
    Serial.print(": ");
    Serial.print(isPositive ? "+" : "-");
    Serial.print(velocity);
    Serial.println();
    
    // TODO: Send as OSC message here
    // sendEncoderOSC(encoderNumber, isPositive ? velocity : -velocity);
  }
}

void processKeypressData(uint8_t count, int slaveIndex, uint8_t address) {
  if (count == 0) return; // No keypress data
  
  Serial.print("[KEY] Slave 0x");
  Serial.print(address, HEX);
  Serial.print(" has ");
  Serial.print(count);
  Serial.println(" key events:");
  
  // Process each key event (3 bytes each)
  for (int i = 0; i < count && Wire.available() >= 3; i++) {
    uint8_t keyHigh = Wire.read();  // High byte of key number
    uint8_t keyLow = Wire.read();   // Low byte of key number
    uint8_t state = Wire.read();    // Key state (1=pressed, 0=released)
    
    // Reconstruct 16-bit key number
    uint16_t keyNumber = (keyHigh << 8) | keyLow;
    
    // Debug output
    Serial.print("  Key ");
    Serial.print(keyNumber);
    Serial.print(": ");
    Serial.println(state ? "PRESSED" : "RELEASED");
    
    // TODO: Send as OSC message here
    // sendKeypressOSC(keyNumber, state);
  }
}

// === NEW FUNCTION: Process encoder button press/release events ===
void processButtonData(uint8_t count, int slaveIndex, uint8_t address) {
  // Exit early if no button events to process
  if (count == 0) return;
  
  // Debug output showing which slave sent button data
  Serial.print("[BTN] Slave 0x");
  Serial.print(address, HEX);
  Serial.print(" has ");
  Serial.print(count);
  Serial.println(" button events:");
  
  // Process each button event (3 bytes each)
  // Protocol format: [encoder_number_high] [encoder_number_low] [button_state]
  for (int i = 0; i < count && Wire.available() >= 3; i++) {
    
    // Read the 3-byte button event
    uint8_t encoderHigh = Wire.read();  // High byte of encoder number (usually 0 for < 256 encoders)
    uint8_t encoderLow = Wire.read();   // Low byte of encoder number
    uint8_t buttonState = Wire.read();  // Button state (1=pressed, 0=released)
    
    // Reconstruct 16-bit encoder number from high and low bytes
    // This allows for up to 65,535 encoder buttons (way more than needed)
    uint16_t encoderNumber = (encoderHigh << 8) | encoderLow;
    
    // Validate button state (should be 0 or 1)
    if (buttonState != BUTTON_PRESSED && buttonState != BUTTON_RELEASED) {
      Serial.print("[ERROR] Invalid button state ");
      Serial.print(buttonState);
      Serial.print(" from encoder ");
      Serial.println(encoderNumber);
      continue; // Skip this invalid event
    }
    
    // Debug output showing button event details
    Serial.print("  Encoder ");
    Serial.print(encoderNumber);
    Serial.print(" button: ");
    
    if (buttonState == BUTTON_PRESSED) {
      Serial.println("PRESSED");
      
      // Handle button press event
      handleEncoderButtonPress(encoderNumber, address);
      
    } else { // buttonState == BUTTON_RELEASED
      Serial.println("RELEASED");
      
      // Handle button release event
      handleEncoderButtonRelease(encoderNumber, address);
    }
    
    // TODO: Send button event as OSC message
    // Example OSC addresses could be:
    // "/encoder/1/button" with value 1 (pressed) or 0 (released)
    // sendButtonOSC(encoderNumber, buttonState);
  }
}

// === NEW FUNCTION: Handle encoder button press events ===
void handleEncoderButtonPress(uint16_t encoderNumber, uint8_t slaveAddress) {
  // This function is called when an encoder button is pressed down
  
  Serial.print("[ACTION] Encoder ");
  Serial.print(encoderNumber);
  Serial.println(" button pressed - starting press action");
  
  // Add your button press logic here:
  // Examples:
  // - Change encoder sensitivity/mode
  // - Trigger a specific function
  // - Start a timer for hold detection
  // - Send OSC message to control software
  // - Change LED state
  // - Switch between different parameter banks
  
  // TODO: Replace with your actual button press handling
  // Example implementations:
  
  // Option 1: Send OSC message
  // char oscAddress[32];
  // snprintf(oscAddress, sizeof(oscAddress), "/encoder/%d/button", encoderNumber);
  // sendOSC(oscAddress, 1); // Send button pressed (1)
  
  // Option 2: Toggle encoder mode
  // toggleEncoderMode(encoderNumber);
  
  // Option 3: Store press time for hold detection
  // encoderButtonPressTime[encoderNumber] = millis();
}

// === NEW FUNCTION: Handle encoder button release events ===
void handleEncoderButtonRelease(uint16_t encoderNumber, uint8_t slaveAddress) {
  // This function is called when an encoder button is released
  
  Serial.print("[ACTION] Encoder ");
  Serial.print(encoderNumber);
  Serial.println(" button released - ending press action");
  
  // Add your button release logic here:
  // Examples:
  // - Calculate how long button was held
  // - Trigger different actions based on hold time
  // - Reset encoder mode to default
  // - Send OSC release message
  // - Update LED state
  // - Confirm parameter changes
  
  // TODO: Replace with your actual button release handling
  // Example implementations:
  
  // Option 1: Send OSC message
  // char oscAddress[32];
  // snprintf(oscAddress, sizeof(oscAddress), "/encoder/%d/button", encoderNumber);
  // sendOSC(oscAddress, 0); // Send button released (0)
  
  // Option 2: Calculate hold time
  // unsigned long holdTime = millis() - encoderButtonPressTime[encoderNumber];
  // if (holdTime > LONG_PRESS_THRESHOLD) {
  //   handleLongPress(encoderNumber);
  // } else {
  //   handleShortPress(encoderNumber);
  // }
  
  // Option 3: Reset encoder mode
  // resetEncoderMode(encoderNumber);
}

// Alternative polling function that measures timing
void measurePollingSpeed() {
  unsigned long startTime = micros();
  
  // Poll all slaves once
  for (int i = 0; i < numSlaves; i++) {
    pollSlave(slaveAddresses[i], i);
  }
  
  unsigned long endTime = micros();
  unsigned long totalTime = endTime - startTime;
  
  Serial.print("[TIMING] Polled ");
  Serial.print(numSlaves);
  Serial.print(" slaves in ");
  Serial.print(totalTime);
  Serial.println(" microseconds");
}