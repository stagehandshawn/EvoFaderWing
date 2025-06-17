// NeoPixelControl.cpp

#include "NeoPixelControl.h"
#include "Utils.h"
#include <stdint.h>  // or <cstdint>
#include <cmath>

//NeoPixel Debug print
bool neoPixelDebug = false;

//================================
// GLOBAL NEOPIXEL OBJECT
//================================

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_RGB + NEO_KHZ800);


// Converts RGB to HSV and scales value, then returns scaled RGB color
uint32_t getScaledColor(const Fader& fader) {
  // Special case: if original color is black (0,0,0), keep it black
  if (fader.red == 0 && fader.green == 0 && fader.blue == 0) {
    return pixels.Color(0, 0, 0);  // Always return black regardless of brightness
  }

  float r = fader.red / 255.0f;
  float g = fader.green / 255.0f;
  float b = fader.blue / 255.0f;

  float cmax = std::max(r, std::max(g, b));
  float cmin = std::min(r, std::min(g, b));
  float delta = cmax - cmin;

  float h = 0, s = 0, v = cmax;

  if (delta != 0) {
    if (cmax == r) h = fmodf(((g - b) / delta), 6.0f);
    else if (cmax == g) h = ((b - r) / delta) + 2.0f;
    else h = ((r - g) / delta) + 4.0f;

    h *= 60.0f;
    if (h < 0) h += 360.0f;
  }

  if (cmax != 0) s = delta / cmax;

  float scaledV = fader.currentBrightness / 255.0f;

  float c = scaledV * s;
  float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
  float m = scaledV - c;

  float r1 = 0, g1 = 0, b1 = 0;

  if (h < 60)      { r1 = c; g1 = x; }
  else if (h < 120){ r1 = x; g1 = c; }
  else if (h < 180){ g1 = c; b1 = x; }
  else if (h < 240){ g1 = x; b1 = c; }
  else if (h < 300){ r1 = x; b1 = c; }
  else             { r1 = c; b1 = x; }

  return pixels.Color(
    (uint8_t)((r1 + m) * 255),
    (uint8_t)((g1 + m) * 255),
    (uint8_t)((b1 + m) * 255)
  );
}



//================================
// SETUP FUNCTION
//================================

void setupNeoPixels() {
  pixels.begin();  // Initialize the NeoPixel strip
  pixels.clear();  // Turn off all pixels
  pixels.show();   // Apply changes

  // Initialize color values in faders
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].red = 255;     // white
    faders[i].green = 255;
    faders[i].blue = 255;
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
      if (elapsed >= Fconfig.fadeTime) {
        f.currentBrightness = f.targetBrightness;
      } else {
        float progress = elapsed / (float)Fconfig.fadeTime;
        int start = f.currentBrightness;
        int delta = (int)f.targetBrightness - start;
        f.currentBrightness = start + (int)(delta * progress);
      }
    }

    uint32_t color = getScaledColor(f);

    if (neoPixelDebug && f.currentBrightness != f.lastReportedBrightness) {
      uint8_t r = (f.red * f.currentBrightness) / 255;
      uint8_t g = (f.green * f.currentBrightness) / 255;
      uint8_t b = (f.blue * f.currentBrightness) / 255;
      debugPrintf("Fader %d RGB → R=%d G=%d B=%d (Brightness=%d)",
                  i, r, g, b, f.currentBrightness);
      f.lastReportedBrightness = f.currentBrightness;
    }

    //pixels.setPixelColor(i * PIXELS_PER_FADER, color);
    for (int j = 0; j < PIXELS_PER_FADER; j++) {
      pixels.setPixelColor(i * PIXELS_PER_FADER + j, color);
    }

  }

  pixels.show();
}

void updateBrightnessOnFaderTouchChange() {
  static bool previousTouch[NUM_FADERS] = { false };

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    bool currentTouch = f.touched;

    if (currentTouch != previousTouch[i]) {
      f.brightnessStartTime = millis();
      f.targetBrightness = currentTouch ? Fconfig.touchedBrightness : Fconfig.baseBrightness;

      if (neoPixelDebug){
          debugPrintf("Fader %d → Touch %s → Brightness target = %d", i,
                  currentTouch ? "TOUCHED" : "released",
                  f.targetBrightness);
      }

      previousTouch[i] = currentTouch;
    }
  }
}


// Update all faders to base brightness if not touched, and reset fade timer.
void updateBaseBrightnessPixels() {
  unsigned long now = millis();

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    if (!f.touched) {
      f.brightnessStartTime = now;
      f.targetBrightness = Fconfig.baseBrightness;
      f.colorUpdated = true;
      // Optionally, set currentBrightness directly if no fade desired:
      // f.currentBrightness = Fconfig.baseBrightness;

      if (neoPixelDebug) {
        debugPrintf("Fader %d base brightness updated to %d", i, Fconfig.baseBrightness);
      }
    }
  }
}