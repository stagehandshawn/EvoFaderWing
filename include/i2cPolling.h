

// i2cPolling.h - Function declarations for I2C polling master
#ifndef I2C_POLLING_H
#define I2C_POLLING_H

#include <Arduino.h>

// Function declarations for I2C polling
void pollSlave(uint8_t address, int slaveIndex);
void processEncoderData(uint8_t count, int slaveIndex, uint8_t address);
void processKeypressData(uint8_t count, int slaveIndex, uint8_t address);
void processButtonData(uint8_t count, int slaveIndex, uint8_t address);
void handleEncoderButtonPress(uint16_t encoderNumber, uint8_t slaveAddress);
void handleEncoderButtonRelease(uint16_t encoderNumber, uint8_t slaveAddress);
void measurePollingSpeed();
void setupI2cPolling();
void handleI2c();



//SIMPLIFED CODE BELOW FOR TESTING


// === ADD THESE DECLARATIONS TO YOUR i2cPolling.h FILE ===
// Add these alongside your existing function declarations

// === SIMPLIFIED I2C POLLING FUNCTIONS ===
// Alternative simplified functions for testing - use instead of regular functions
void setupI2cPollingSimple();
void handleI2cSimple();
void pollSlaveSimpleVersion(uint8_t address, int slaveIndex);
void processEncoderDataSimpleVersion(uint8_t count, uint8_t address);
void processKeypressDataSimpleVersion(uint8_t count, uint8_t address);

// === EXAMPLE OF COMPLETE HEADER FILE ===
// Your i2cPolling.h should look something like this:

/*
#ifndef I2C_POLLING_H
#define I2C_POLLING_H

#include <stdint.h>

// === ORIGINAL FUNCTIONS ===
void setupI2cPolling();
void handleI2c();
void pollSlave(uint8_t address, int slaveIndex);
void processEncoderData(uint8_t count, int slaveIndex, uint8_t address);
void processKeypressData(uint8_t count, int slaveIndex, uint8_t address);
void measurePollingSpeed();

// === SIMPLIFIED FUNCTIONS (for testing) ===
void setupI2cPollingSimple();
void handleI2cSimple();
void pollSlaveSimpleVersion(uint8_t address, int slaveIndex);
void processEncoderDataSimpleVersion(uint8_t count, uint8_t address);
void processKeypressDataSimpleVersion(uint8_t count, uint8_t address);

#endif
*/


#endif  // I2C_POLLING_H


