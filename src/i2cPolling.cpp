// === TEENSY 4.1 I2C POLLING MASTER ===
// Polls 5 ATmega slaves for keyboard and encoder data ONLY
// No interrupt pins needed - pure polling approach
// Simplified version - handles ONLY encoder rotation and keypress events
// NO button press handling - encoders send rotation data, keyboard sends key events
// Added: Reset signal detection from keyboard slave during startup

#include <Wire.h>
#include "i2cPolling.h"
#include "Utils.h"
#include "EEPROMStorage.h"

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
#define DATA_TYPE_BUTTON   0x03  // Data type identifier for button messages (future use)
#define DATA_TYPE_RESET    0x04  // Data type identifier for reset signal (startup detection)

// Note: DATA_TYPE_BUTTON (0x03) removed - we don't handle button presses in this version

// === Timing Control Variables ===
unsigned long lastPollTime = 0;          // Timestamp of the last polling cycle
const unsigned long I2C_POLL_INTERVAL = 5;    // 5ms

// === INITIALIZATION FUNCTION ===
// Call this inside of your main setup() function
// This replaces the standalone setup() that would be in a separate sketch
void setupI2cPolling() {
  // Initialize I2C communication as master device
  Wire.begin();                // Start I2C bus as master (no address needed for master)
  Wire.setClock(400000);       // Set I2C clock to 400kHz (fast mode for better performance)
  
  // Use project's debug system for consistent output
  debugPrint("[MASTER] Teensy I2C polling master started");
  debugPrintf("Polling %d slaves for encoder and keypress data...", numSlaves);
  
  // Display all slave addresses that will be polled for verification
  for (int i = 0; i < numSlaves; i++) {
    // Add descriptive text for each slave type
    if (slaveAddresses[i] == I2C_ADDR_KEYBOARD) {
      debugPrintf("  Slave %d: 0x%02X (Keyboard Matrix)", i, slaveAddresses[i]);
    } else {
      debugPrintf("  Slave %d: 0x%02X (Encoder Group)", i, slaveAddresses[i]);
    }
  }
  
  debugPrint("[INFO] Ready to receive encoder rotations and key presses");

  // Poll keyboard slave during setup to check for reset signal
  // This happens once at startup to detect if reset key was held during power-on
  debugPrint("[SETUP] Checking keyboard slave for reset signal...");
  pollSlave(I2C_ADDR_KEYBOARD, 0);  // Poll keyboard slave on its own at setup to check for reset command
}

// === MAIN POLLING FUNCTION ===
// Call this inside your main loop() function
// This replaces the standalone loop() that would be in a separate sketch
void handleI2c() { 
  unsigned long currentTime = millis();  // Get current timestamp
  
  // Check if enough time has passed since last polling cycle
  // This implements timing control without blocking other operations
  if (currentTime - lastPollTime >= I2C_POLL_INTERVAL) {
    lastPollTime = currentTime;  // Update timestamp for next cycle
    
    // Poll each slave device in sequence to check for new data
    // Each slave will be asked if it has any encoder or keypress events
    for (int i = 0; i < numSlaves; i++) {
      pollSlave(slaveAddresses[i], i);  // Poll individual slave
    }
  }
  
  // Note: This function returns quickly (within ~2.5ms total for all slaves)
  // Plenty of time remaining in each loop cycle for other tasks like:
  // - Fader control processing
  // - OSC message handling  
  // - Web server requests
  // - NeoPixel updates
  // - Touch sensor processing
}

// === INDIVIDUAL SLAVE POLLING FUNCTION ===
// Communicates with a single I2C slave to request and process data
void pollSlave(uint8_t address, int slaveIndex) {
  // Request up to 32 bytes of data from the specified slave
  // The slave will send whatever data it has available (encoder or keypress events)
  Wire.requestFrom(address, static_cast<uint8_t>(32));
  
  // Check if the slave responded with at least the minimum required data
  // Every valid response needs at least 2 bytes: [data_type][count]
  if (Wire.available() < 2) {
    // Slave didn't respond or didn't send enough data
    // This is normal - slaves only respond when they have events to report
    return;
  }
  
  // Read the standardized protocol header that all slaves use
  uint8_t dataType = Wire.read();  // First byte: identifies what type of data follows
  uint8_t count = Wire.read();     // Second byte: how many events are in this message

  int remaining = Wire.available();

  if (dataType == DATA_TYPE_ENCODER && remaining < count * 2) {
    debugPrintf("[WARN] Incomplete encoder packet from slave 0x%02X: expected %d, got %d", address, count * 2, remaining);
    while (Wire.available()) Wire.read();
    return;
  }

  if (dataType == DATA_TYPE_KEYPRESS && remaining < count * 3) {
    debugPrintf("[WARN] Incomplete keypress packet from slave 0x%02X: expected %d, got %d", address, count * 3, remaining);
    while (Wire.available()) Wire.read();
    return;
  }

  if (count > 32) {
    debugPrintf("[WARN] Unrealistic count (%d) from slave 0x%02X", count, address);
    while (Wire.available()) Wire.read();
    return;
  }
  
  // Process the data based on its type using the protocol identifier
  switch (dataType) {
    case DATA_TYPE_ENCODER:
      // Handle encoder rotation events (direction and velocity)
      processEncoderData(count, slaveIndex, address);
      while (Wire.available()) Wire.read(); // Clean up leftovers
      break;
      
    case DATA_TYPE_KEYPRESS:
      // Handle keyboard matrix events (key press and release)
      processKeypressData(count, slaveIndex, address);
      while (Wire.available()) Wire.read(); // Clean up leftovers
      break;

    case DATA_TYPE_RESET:
      // Handle reset signal from keyboard slave (sent once during startup if reset key held)
      // Reset data format: [DATA_TYPE_RESET][1][key_high][key_low][state]
      // Validate that we have the expected reset data format
      if (count == 1 && Wire.available() >= 3) {
        // Read the reset data payload - same format as keypress data
        uint8_t keyHigh = Wire.read();  // High byte of reset key number
        uint8_t keyLow = Wire.read();   // Low byte of reset key number  
        uint8_t state = Wire.read();    // Key state: should be 1 for reset
        
        // Reconstruct the full key number for verification
        uint16_t keyNumber = (keyHigh << 8) | keyLow;
        
        // Validate that this is a proper reset signal
        if (state == 1) {  // Reset key was pressed during startup
          debugPrintf("[RESET] Reset signal received from slave 0x%02X", address);
          debugPrintf("[RESET] Reset key number: %d", keyNumber);
          
          resetNetworkDefaults();
          
          debugPrint("[RESET] Network reset triggered");
        } else {
          debugPrintf("[RESET] Invalid reset state: %d (expected 1)", state);
        }
      } else {
        // Invalid reset data format - wrong count or insufficient bytes
        debugPrintf("[RESET] Invalid reset data: count=%d, available=%d", count, Wire.available());
        // Clear any remaining bytes to prevent I2C buffer issues
        while (Wire.available()) Wire.read();
      }
      while (Wire.available()) Wire.read(); // Clean up leftovers
      break;
      
    default:
      // Unknown or unsupported data type received
      // Clear any remaining bytes to prevent I2C buffer issues
      while (Wire.available()) Wire.read();
      
      // Log the error for debugging using project's debug system
      debugPrintf("[ERROR] Unknown data type 0x%02X from slave 0x%02X", dataType, address);
      break;
  }
}

// === ENCODER DATA PROCESSING FUNCTION ===
// Processes encoder rotation events received from encoder slave devices
void processEncoderData(uint8_t count, int slaveIndex, uint8_t address) {
  // Exit early if no encoder events to process
  if (count == 0) return;
  
  // Debug output showing which slave sent encoder data
  debugPrintf("[ENC] Slave 0x%02X has %d encoder rotation events:", address, count);
  
  // Process each encoder event individually
  // Each encoder event consists of exactly 2 bytes: [encoder_with_direction][velocity]
  for (int i = 0; i < count && Wire.available() >= 2; i++) {
    // Read the 2-byte encoder event
    uint8_t encoderWithDir = Wire.read();  // Encoder index with direction bit encoded
    uint8_t velocity = Wire.read();        // Rotation velocity/speed value
    
    // Decode the encoder information from the first byte
    // Lower 7 bits = encoder number (0-127, more than enough for our system)
    // Upper 1 bit = direction flag (0=negative/left, 1=positive/right)
    uint8_t encoderNumber = encoderWithDir & 0x7F;  // Mask to get lower 7 bits
    bool isPositive = (encoderWithDir & 0x80) != 0; // Check if MSB is set
    
    if (encoderNumber > 19) {
      debugPrintf("[WARN] Ignoring invalid encoder number: %d", encoderNumber);
      continue;
    }
    if (velocity > 10) {
      debugPrintf("[WARN] Ignoring suspicious velocity: %d", velocity);
      continue;
    }
    debugPrintf("  Encoder %d: %s%d", encoderNumber, isPositive ? "+" : "-", velocity);
    
    // TODO: Convert encoder event to OSC message and send
    // This is where you would integrate with your OSC sending system
    // Example: sendEncoderOSC(encoderNumber, isPositive ? velocity : -velocity);
    
    // TODO: You might also want to:
    // - Map encoder numbers to specific fader controls
    // - Apply scaling factors for different encoder sensitivities  
    // - Filter out very small movements to reduce noise
    // - Send to different OSC addresses based on encoder number ranges
  }
}

// === KEYPRESS DATA PROCESSING FUNCTION ===
// Processes keyboard matrix events received from keyboard slave device
void processKeypressData(uint8_t count, int slaveIndex, uint8_t address) {
  // Exit early if no keypress events to process
  if (count == 0) return;
  
  // Debug output showing which slave sent keypress data
  debugPrintf("[KEY] Slave 0x%02X has %d key events:", address, count);
  
  // Process each keypress event individually
  // Each keypress event consists of exactly 3 bytes: [key_high][key_low][state]
  for (int i = 0; i < count && Wire.available() >= 3; i++) {
    // Read the 3-byte keypress event
    uint8_t keyHigh = Wire.read();  // High byte of 16-bit key number
    uint8_t keyLow = Wire.read();   // Low byte of 16-bit key number
    uint8_t state = Wire.read();    // Key state: 1=pressed down, 0=released
    
    // Reconstruct the full 16-bit key number from high and low bytes
    // This allows for key numbers up to 65,535 (your system uses 101-410)
    uint16_t keyNumber = (keyHigh << 8) | keyLow;  // Combine bytes into 16-bit value
    
    // Debug output showing the decoded keypress event
    debugPrintf("  Key %d: %s", keyNumber, state ? "PRESSED" : "RELEASED");
    
    // TODO: Convert keypress event to OSC message and send
    // This is where you would integrate with your OSC sending system
    // Example: sendKeypressOSC(keyNumber, state);
    
    // TODO: You might also want to:
    // - Map specific key numbers to special functions
    // - Handle key combinations or sequences
    // - Implement different behavior for press vs release
    // - Use certain keys as "shift" or "mode" keys
    // - Send different OSC addresses based on key number ranges
  }
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






//SIMPLIFIED CODE BELOW!!!



// === SIMPLIFIED I2C POLLING - ADD TO EXISTING CODE ===
// Add this to your existing i2cPolling.cpp file
// Use handleI2cSimple() instead of handleI2c() in your main loop to test
// All function names are different to avoid conflicts

// === Simplified Timing Variables (separate from original) ===
unsigned long lastPollTimeSimple = 0;          
const unsigned long I2C_POLL_INTERVAL_SIMPLE = 5;    // Poll every 5ms instead of 1ms

// === SIMPLIFIED SETUP FUNCTION ===
// Call this INSTEAD of setupI2cPolling() in your main setup()
void setupI2cPollingSimple() {
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

// === SIMPLIFIED MAIN POLLING FUNCTION ===
// Call this INSTEAD of handleI2c() in your main loop()
void handleI2cSimple() { 
  unsigned long currentTime = millis();  
  
  if (currentTime - lastPollTimeSimple >= I2C_POLL_INTERVAL_SIMPLE) {
    lastPollTimeSimple = currentTime;  
    
    // Poll each slave with extra safety
    for (int i = 0; i < numSlaves; i++) {
      pollSlaveSimpleVersion(slaveAddresses[i], i);  
      delay(1); // Small delay between slave polls
    }
  }
}

// === SIMPLIFIED SLAVE POLLING ===
void pollSlaveSimpleVersion(uint8_t address, int slaveIndex) {
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
  if (dataType != DATA_TYPE_ENCODER && 
      dataType != DATA_TYPE_KEYPRESS && 
      dataType != DATA_TYPE_RESET) {
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
      processEncoderDataSimpleVersion(count, address);
      break;
      
    case DATA_TYPE_KEYPRESS:
      processKeypressDataSimpleVersion(count, address);
      break;
      
    case DATA_TYPE_RESET:
      debugPrint("[I2C] Reset signal received");
      // NETWORK  RESET CODE HERE
      break;
  }
  
  // Clean up any remaining bytes
  while (Wire.available()) Wire.read();
}

// === SIMPLIFIED ENCODER PROCESSING ===
void processEncoderDataSimpleVersion(uint8_t count, uint8_t address) {
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
    
    if (isPositive) {
          // Positve move
    } else {
          // Negative move
    }
  }
}

// === SIMPLIFIED KEYPRESS PROCESSING ===
void processKeypressDataSimpleVersion(uint8_t count, uint8_t address) {
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
    

    // placeholder: Handle key press or release for OSC sending
    if (state == 1) {
      // TODO: Send OSC packet for key press event
    } else {
      // TODO: Send OSC packet for key release event
    }


  }
}

/*
=== HOW TO USE ===

In your main setup(), replace:
  setupI2cPolling();
with:
  setupI2cPollingSimple();

In your main loop(), replace:
  handleI2c();
with:
  handleI2cSimple();

To switch back to original, just change the function calls back.
The original functions are still there and unchanged.

*/