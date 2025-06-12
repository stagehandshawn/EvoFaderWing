// i2cPolling.h - Function declarations for I2C polling master
#ifndef I2C_POLLING_H
#define I2C_POLLING_H

#include <Arduino.h>

void setupI2cPolling();
void handleI2c();
void pollSlave(uint8_t address, int slaveIndex);
void processEncoderData(uint8_t count, uint8_t address);
void processKeypressData(uint8_t count, uint8_t address);

void sendEncoderOSC(int encoderNumber, bool isPositive, int velocity);
void sendKeyOSC(uint16_t keyNumber, uint8_t state);

#endif  // I2C_POLLING_H


