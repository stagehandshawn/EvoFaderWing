// i2cPolling.cpp

// === TEENSY 4.1 I2C POLLING MASTER ===
// Polls 5 ATmega slaves for keyboard and encoder data ONLY
// No interrupt pins needed - pure polling approach
// Simplified version - handles ONLY encoder rotation and keypress events
// NO button press handling - encoders send rotation data, keyboard sends key events

#include <Wire.h>
#include "i2cPolling.h"
#include "Utils.h"
#include "EEPROMStorage.h"
#include "NetworkOSC.h"

// === I2C Slave Addresses ===
#define I2C_ADDR_KEYBOARD  0x10  // Keyboard matrix ATmega - sends keypress data
#define I2C_ADDR_ENCODER1  0x11  // First encoder ATmega (5 encoders) - sends rotation data
#define I2C_ADDR_ENCODER2  0x12  // Second encoder ATmega (5 encoders) - sends rotation data
#define I2C_ADDR_ENCODER3  0x13  // Third encoder ATmega (5 encoders) - sends rotation data
#define I2C_ADDR_ENCODER4  0x14  // Fourth encoder ATmega (5 encoders) - sends rotation data

// Array of all slave addresses for easy iteration during polling
const uint8_t slaveAddresses[] = {
  I2C_ADDR_KEYBOARD,   // First slave: keyboard matrix for key events
  I2C_ADDR_ENCODER1,   // Second slave: encoders 0-4
  I2C_ADDR_ENCODER2,   // Third slave: encoders 5-9
  I2C_ADDR_ENCODER3,   // Fourth slave: encoders 10-14
  I2C_ADDR_ENCODER4    // Fifth slave: encoders 15-19
};
const int numSlaves = sizeof(slaveAddresses) / sizeof(slaveAddresses[0]);

// === Protocol Constants ===
// These constants define the message types that slaves can send
#define DATA_TYPE_ENCODER  0x01  // Data type identifier for encoder rotation messages
#define DATA_TYPE_KEYPRESS 0x02  // Data type identifier for keypress/release messages

// === Timing Variables ===
unsigned long lastPollTimeSimple = 0;          
const unsigned long I2C_POLL_INTERVAL_SIMPLE = 10;    // Poll every 10ms instead of 1ms

// === SETUP FUNCTION ===

void setupI2cPolling() {
  Wire.begin();                
  Wire.setClock(400000);       // 400kHz
  
  debugPrint("[I2C] Polling Init");
  debugPrintf("Polling %d slaves every %lums...", numSlaves, I2C_POLL_INTERVAL_SIMPLE);
  
  for (int i = 0; i < numSlaves; i++) {
    if (slaveAddresses[i] == I2C_ADDR_KEYBOARD) {
      debugPrintf("  Slave %d: 0x%02X (Keyboard Matrix)", i, slaveAddresses[i]);
    } else {
      debugPrintf("  Slave %d: 0x%02X (Encoder Group)", i, slaveAddresses[i]);
    }
  }
  
  debugPrint("[I2C] Ready for polling");
}

// === MAIN POLLING FUNCTION ===

void handleI2c() { 
  unsigned long currentTime = millis();  
  
  if (currentTime - lastPollTimeSimple >= I2C_POLL_INTERVAL_SIMPLE) {
    lastPollTimeSimple = currentTime;  
    
    // Poll each slave with extra safety
    for (int i = 0; i < numSlaves; i++) {
      pollSlave(slaveAddresses[i], i);  
      delay(1); // Small delay between slave polls
    }
  }
}

// === SLAVE POLLING ===
void pollSlave(uint8_t address, int slaveIndex) {
  // Clear any leftover data first
  while (Wire.available()) Wire.read();
  
  // Request data from slave
  uint8_t bytesRequested = 16; // Smaller request size
  Wire.requestFrom(address, bytesRequested);
  
  // Wait a bit for response
  delay(1);
  
  // Check if we got minimum required data
  if (Wire.available() < 2) {
    return; // No data or insufficient data
  }
  
  // Read header
  uint8_t dataType = Wire.read();  
  uint8_t count = Wire.read();     
  
  // Validate data type first
  if (dataType != DATA_TYPE_ENCODER && dataType != DATA_TYPE_KEYPRESS){
    debugPrintf("[I2C] ERR Invalid data type 0x%02X from slave 0x%02X", dataType, address);
    // Clear buffer and return
    while (Wire.available()) Wire.read();
    return;
  }
  
  // Validate count
  if (count > 10) {  // Reasonable maximum
    debugPrintf("[I2C] ERR Unrealistic count %d from slave 0x%02X", count, address);
    while (Wire.available()) Wire.read();
    return;
  }
  
  // Validate we have enough bytes for the claimed count
  int bytesPerEvent = (dataType == DATA_TYPE_ENCODER) ? 2 : 3;
  int expectedBytes = count * bytesPerEvent;
  
  if (count > 0 && Wire.available() < expectedBytes) {
    debugPrintf("[I2C] ERR Not enough data: need %d, have %d from slave 0x%02X", 
               expectedBytes, Wire.available(), address);
    while (Wire.available()) Wire.read();
    return;
  }
  
  // Additional validation: keyboard should never send encoder data
  if (address == I2C_ADDR_KEYBOARD && dataType == DATA_TYPE_ENCODER) {
    debugPrintf("[I2C] ERR Keyboard slave 0x%02X sent encoder data - corrupted!", address);
    while (Wire.available()) Wire.read();
    return;
  }
  
  // Process the validated data
  switch (dataType) {
    case DATA_TYPE_ENCODER:
      processEncoderData(count, address);
      break;
      
    case DATA_TYPE_KEYPRESS:
      processKeypressData(count, address);
      break;

  }
  
  // Clean up any remaining bytes
  while (Wire.available()) Wire.read();
}

// === ENCODER PROCESSING ===
void processEncoderData(uint8_t count, uint8_t address) {
  if (count == 0) return;
  
  debugPrintf("[ENC] Slave 0x%02X: %d encoder events", address, count);
  
  for (int i = 0; i < count; i++) {
    if (Wire.available() < 2) {
      debugPrint("[I2C] ERR Not enough encoder data");
      break;
    }
    
    uint8_t encoderWithDir = Wire.read();  
    uint8_t velocity = Wire.read();        
    
    uint8_t encoderNumber = encoderWithDir & 0x7F;  
    bool isPositive = (encoderWithDir & 0x80) != 0; 
    
    // Validate encoder number and velocity
    if (encoderNumber > 20) {
      debugPrintf("[I2C] WARN Invalid encoder number: %d", encoderNumber);
      continue;
    }
    if (velocity > 10) {
      debugPrintf("[I2C] WARN Invalid velocity: %d", velocity);
      continue;
    }
    
    debugPrintf("  Encoder %d: %s%d", encoderNumber, isPositive ? "+" : "-", velocity);
    
    sendEncoderOSC(encoderNumber, isPositive, velocity);                                   //ENCODER OSC SEND
  }
}

// === KEYPRESS PROCESSING ===
void processKeypressData(uint8_t count, uint8_t address) {
  if (count == 0) return;
  
  debugPrintf("[KEY] Slave 0x%02X: %d key events", address, count);
  
  for (int i = 0; i < count; i++) {
    if (Wire.available() < 3) {
      debugPrint("[I2C] WARN Not enough keypress data");
      break;
    }
    
    uint8_t keyHigh = Wire.read();  
    uint8_t keyLow = Wire.read();   
    uint8_t state = Wire.read();    
    
    uint16_t keyNumber = (keyHigh << 8) | keyLow;  
    
    // Validate key number and state
    if (keyNumber < 101 || keyNumber > 410) {
      debugPrintf("[I2C] WARN Invalid key number: %d", keyNumber);
      continue;
    }
    if (state > 1) {
      debugPrintf("[I2C] WARN Invalid key state: %d", state);
      continue;
    }
    
    debugPrintf("  Key %d: %s", keyNumber, state ? "PRESSED" : "RELEASED");
    
        // Check for reset condition: key 401 pressed during startup window
    if (checkForReset && keyNumber == 401 && state == 1) {
      debugPrint("[NETWORK RESET]");
      displayShowResetHeader();
      delay(3000);
      resetNetworkDefaults();
      checkForReset = false;  // Prevent multiple resets
      return;  // Skip further processing of this event
    }

      sendKeyOSC(keyNumber, state);

  }
}


void sendEncoderOSC(int encoderNumber, bool isPositive, int velocity) { 

    // Validate encoder number
  if (encoderNumber > 20) {
    debugPrintf("[OSC] Invalid encoder number: %d", encoderNumber);
    return;
  }

    // Create the OSC address based on encoder number mapping
  char oscAddress[32];
  int executorKnobNumber;
  if (encoderNumber < 11) {
      // Encoders 0-9 map to ExecutorKnob401-410
    executorKnobNumber = 400 + encoderNumber;
  } else {
      // Encoders 10-19 map to ExecutorKnob301-310
    executorKnobNumber = 300 + (encoderNumber - 10);
  }

    // Format the OSC address
  snprintf(oscAddress, sizeof(oscAddress), "/Encoder%d", executorKnobNumber);
    // Create signed velocity value
  int signedVelocity = isPositive ? (int)velocity : -(int)velocity;

    // Send the OSC message
  sendOscMessage(oscAddress, ",i", &signedVelocity);

    // Debug output
  debugPrintf("[OSC] Sent: %s %d (encoder %d)", oscAddress, signedVelocity, encoderNumber);

}


void sendKeyOSC(uint16_t keyNumber, uint8_t state) {
  // Validate key number is in expected ranges
  if (!((keyNumber >= 101 && keyNumber <= 110) ||
        (keyNumber >= 201 && keyNumber <= 210) ||
        (keyNumber >= 301 && keyNumber <= 310) ||
        (keyNumber >= 401 && keyNumber <= 410))) {
    debugPrintf("[OSC] Invalid key number for OSC: %d", keyNumber);
    return;
  }
  
  // Validate state
  if (state > 1) {
    debugPrintf("[OSC] Invalid key state: %d", state);
    return;
  }
  
  // Create the OSC address
  char oscAddress[32];
  snprintf(oscAddress, sizeof(oscAddress), "/Key%d", keyNumber);
  
  // Convert state to int for OSC message
  int keyState = (int)state;
  
  // Send the OSC message
  sendOscMessage(oscAddress, ",i", &keyState);
  
  // Debug output
  debugPrintf("[OSC] Sent: %s %d (key %d %s)", 
             oscAddress, keyState, keyNumber, state ? "PRESSED" : "RELEASED");
}





// === PERFORMANCE MEASUREMENT FUNCTION ===
// Alternative polling function that measures the time taken to poll all slaves
// Useful for performance tuning and verifying that polling stays within timing budget
void measurePollingSpeed() {
  unsigned long startTime = micros();  // Get high-precision start timestamp
  
  // Poll all slaves once, just like the normal polling cycle
  for (int i = 0; i < numSlaves; i++) {
    pollSlave(slaveAddresses[i], i);
  }
  
  unsigned long endTime = micros();    // Get high-precision end timestamp
  unsigned long totalTime = endTime - startTime;  // Calculate elapsed time
  
  // Output timing information for performance analysis using project's debug system
  debugPrintf("[TIMING] Polled %d slaves in %lu microseconds", numSlaves, totalTime);
  
  // Note: With 5 slaves at 400kHz I2C, total time should be around 2000-3000 microseconds
  // This leaves plenty of time in each 1ms polling cycle for other system operations
}