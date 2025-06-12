#ifndef FADER_CONTROL_H
#define FADER_CONTROL_H

#include <Arduino.h>
#include "Config.h"

//================================
// FUNCTION DECLARATIONS
//================================

// Main fader processing
void handleFaders();

//Fader movement
void moveFaderToSetpoint(Fader& f);

// Fader initialization
void initializeFaders();
void configureFaderPins();

// Motor control
void driveMotor(Fader& f, int direction);

// Position reading and filtering
int readSmoothedPosition(Fader& f);

// State management
void updateFaderState(Fader& f);

// Calibration
void calibrateFaders();

void handleFadersSimple();
void moveAllFadersToSetpoints();

void setFaderSetpoint(int faderIndex, int oscValue);

int readFaderOSC(Fader& f);

#endif // FADER_CONTROL_H