// KeyLedControl.h
#ifndef KEY_LED_CONTROL_H
#define KEY_LED_CONTROL_H

#include <Arduino.h>
#include "Config.h"

// Setup and periodic refresh for the executor key LED strip
void setupKeyLeds();
void updateKeyLeds();

// Call when executor states change so the LEDs can refresh on the next loop
void markKeyLedsDirty();

// Push a temporary startup animation frame to executor LEDs.
// colors10 is indexed by fader slot (0..9) and repeats across all 4 rows.
void showExecutorStartupFromFaderColors(const uint8_t colors10[NUM_FADERS][3]);

#endif // KEY_LED_CONTROL_H
