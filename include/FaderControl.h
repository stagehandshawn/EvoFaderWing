// FaderControl.h
#ifndef FADER_CONTROL_H
#define FADER_CONTROL_H

#include <Arduino.h>
#include "Config.h"


// Fader initialization
void initializeFaders();
void configureFaderPins();
void calibrateFaders();

// Main fader processing
void handleFaders();


//Fader movement
void driveMotor(Fader& f, int direction);

void setFaderSetpoint(int faderIndex, int oscValue);
int readFadertoOSC(Fader& f);

void moveAllFadersToSetpoints();

#endif // FADER_CONTROL_H