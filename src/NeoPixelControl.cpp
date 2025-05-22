#include "NeoPixelControl.h"
#include "Utils.h"

//================================
// GLOBAL NEOPIXEL OBJECT
//================================

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);



//================================
// SETUP FUNCTION
//================================

void setupNeoPixels() {
  pixels.begin();  // Initialize the NeoPixel strip
  pixels.clear();  // Turn off all pixels
  pixels.show();   // Apply changes

  // Initialize color values in faders
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].red = 60;     // Default to dim white
    faders[i].green = 60;
    faders[i].blue = 60;
    faders[i].colorUpdated = true;  // Force initial update
  }
}

//================================
// MAIN UPDATE FUNCTION
//================================

void updateNeoPixels() {
  unsigned long now = millis();

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];

    // Calculate fade progress for brightness transitions
    if (f.currentBrightness != f.targetBrightness) {
      unsigned long elapsed = now - f.brightnessStartTime;
      if (elapsed >= fadeTime) {
        f.currentBrightness = f.targetBrightness;
      } else {
        float progress = elapsed / (float)fadeTime;
        int start = f.currentBrightness;
        int delta = (int)f.targetBrightness - start;
        f.currentBrightness = start + (int)(delta * progress);
      }
    }

    // Apply brightness-scaled RGB
    uint8_t r = (f.red * f.currentBrightness) / 255;
    uint8_t g = (f.green * f.currentBrightness) / 255;
    uint8_t b = (f.blue * f.currentBrightness) / 255;
    uint32_t color = pixels.Color(r, g, b);

    // Only output debug when brightness changes
    if (f.currentBrightness != f.lastReportedBrightness) {
      debugPrintf("Fader %d RGB → R=%d G=%d B=%d (Brightness=%d)",
                  i, r, g, b, f.currentBrightness);
      f.lastReportedBrightness = f.currentBrightness;
    }

    pixels.setPixelColor(i * PIXELS_PER_FADER, color);
  }

  pixels.show();
}