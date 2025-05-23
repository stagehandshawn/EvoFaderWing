

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


#endif  // I2C_POLLING_H