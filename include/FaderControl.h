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
void setFaderSetpoint(int faderIndex, int oscValue);
void moveAllFadersToSetpoints();
void resetFaderOwnership();  // Clear manualOverride on all faders (call on desk unlock)


int readFadertoOSC(Fader& f);
int getFaderIndexFromID(int id);

// Fader retry
void checkFaderRetry();

#endif // FADER_CONTROL_H