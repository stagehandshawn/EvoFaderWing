// NeoPixelControl.h
#ifndef NEOPIXEL_CONTROL_H
#define NEOPIXEL_CONTROL_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"

//================================
// GLOBAL NEOPIXEL OBJECT
//================================

extern Adafruit_NeoPixel pixels;

//================================
// FUNCTION DECLARATIONS
//================================

// Setup and main update
void setupNeoPixels();
void updateNeoPixels();

#endif // NEOPIXEL_CONTROL_H