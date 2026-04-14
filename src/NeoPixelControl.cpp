// NeoPixelControl.cpp

#include "NeoPixelControl.h"
#include "Utils.h"
#include "FaderControl.h"
#include "KeyLedControl.h"
#include <stdint.h>  // or <cstdint>
#include <cmath>

//================================
// GLOBAL NEOPIXEL OBJECT
//================================

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_RGB + NEO_KHZ800);


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
    faders[i].lastRenderedColor = 0xFFFFFFFF; // force initial render
    faders[i].lastRenderedSetpoint = 255;     // force initial render
    //faders[i].colorUpdated = true;  // Force initial update
  }
}

//================================
// MAIN UPDATE FUNCTION
//================================

void updateNeoPixels() {
  unsigned long now = millis();
  bool pixelsDirty = false;

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
    bool needsUpdate = false;

    if (f.currentBrightness != f.lastReportedBrightness) {
      f.lastReportedBrightness = f.currentBrightness;
    }

    if (!Fconfig.useLevelPixels) {
      needsUpdate = (color != f.lastRenderedColor);
      // Legacy mode: fill all pixels for this fader with the scaled color
      for (int j = 0; j < PIXELS_PER_FADER; j++) {
        pixels.setPixelColor(i * PIXELS_PER_FADER + j, color);
      }
    } else {
      // Level mode: light up pixels per side based on current fader position (0-100)
      // Use the current setpoint (OSC value 0-100) to avoid analog jitter
      int oscValue = constrain(f.setpoint, 0, 100);
      // Round to nearest and clamp so the bottom pixel stays lit
      int litPerSide = (oscValue * 12 + 50) / 100;  // map 0-100 to 0-12 (rounded)
      litPerSide = constrain(litPerSide, 1, 12);    // always show at least the bottom pixel
      needsUpdate = (color != f.lastRenderedColor) || (f.lastRenderedSetpoint != (uint8_t)oscValue);

      for (int j = 0; j < PIXELS_PER_FADER; j++) {
        bool isLit = false;
        if (j < 12) {
          int rel = 11 - j;           // left side, bottom = rel 0
          isLit = rel < litPerSide;
        } else {
          int rel = j - 12;           // right side, bottom = rel 0
          isLit = rel < litPerSide;
        }
        pixels.setPixelColor(i * PIXELS_PER_FADER + j, isLit ? color : pixels.Color(0, 0, 0));
      }
      f.lastRenderedSetpoint = (uint8_t)oscValue;
    }

    if (needsUpdate) {
      f.lastRenderedColor = color;
      pixelsDirty = true;
    }
  }

  // Push to strip when something changed
  if (pixelsDirty) {
    pixels.show();
  }
}

void invalidateNeoPixelRenderCache() {
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].lastRenderedColor = 0xFFFFFFFF;
    faders[i].lastRenderedSetpoint = 255;
  }
}

void updateBrightnessOnFaderTouchChange() {
  static bool previousTouch[NUM_FADERS] = { false };

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    bool currentTouch = f.touched;

    if (currentTouch != previousTouch[i]) {
      f.brightnessStartTime = millis();
      f.targetBrightness = currentTouch ? Fconfig.touchedBrightness : Fconfig.baseBrightness;

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
      // Optionally, set currentBrightness directly if no fade desired:
      // f.currentBrightness = Fconfig.baseBrightness;

    }
  }
}



//================================
// Color functions
//================================

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

  float h = 0, s = 0;//, v = cmax;  //we are not using value in HSV, we are using fader.currentBrightness as v

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



void fadeSequence(unsigned long STAGGER_DELAY, unsigned long COLOR_CYCLE_TIME) {
  
  unsigned long startTime = millis();
  bool animationComplete = false;
  uint8_t originalColors[NUM_FADERS][3];
  uint8_t startupColors10[NUM_FADERS][3];
  
  // Start all faders at black (0,0,0) with zero brightness
  for (int i = 0; i < NUM_FADERS; i++) {
    
    originalColors[i][0] = faders[i].red;
    originalColors[i][1] = faders[i].green;
    originalColors[i][2] = faders[i].blue;
    

    faders[i].currentBrightness = 0;
    faders[i].red = 0;
    faders[i].green = 0;
    faders[i].blue = 0;
  }
  
  while (!animationComplete) {
    unsigned long now = millis();
    animationComplete = true;
    
    for (int i = 0; i < NUM_FADERS; i++) {
      unsigned long faderStartTime = startTime + (i * STAGGER_DELAY);
      
      if (now >= faderStartTime) {
        unsigned long elapsed = now - faderStartTime;
        
        if (elapsed < COLOR_CYCLE_TIME) {
          // Color wave is active on this fader
          animationComplete = false;
          
          // Calculate color transition through rainbow spectrum
          float colorProgress = (elapsed % COLOR_CYCLE_TIME) / (float)COLOR_CYCLE_TIME;
          float hue = colorProgress * 360.0; // Full rainbow cycle
          
          // Convert HSV to RGB (Hue, Saturation=1, Value=1)
          float c = 1.0; // Saturation at max
          float x = c * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
          float m = 0.0;
          
          float r1, g1, b1;
          if (hue < 60)      { r1 = c; g1 = x; b1 = 0; }
          else if (hue < 120){ r1 = x; g1 = c; b1 = 0; }
          else if (hue < 180){ r1 = 0; g1 = c; b1 = x; }
          else if (hue < 240){ r1 = 0; g1 = x; b1 = c; }
          else if (hue < 300){ r1 = x; g1 = 0; b1 = c; }
          else             { r1 = c; g1 = 0; b1 = x; }
          
          // Set the color
          faders[i].red = (uint8_t)((r1 + m) * 255);
          faders[i].green = (uint8_t)((g1 + m) * 255);
          faders[i].blue = (uint8_t)((b1 + m) * 255);
          
          // Calculate brightness with breathing effect
          float breatheProgress = (elapsed % COLOR_CYCLE_TIME) / (float)COLOR_CYCLE_TIME;
          float breatheValue = (sin(breatheProgress * PI * 2) + 1.0) / 2.0; // Sine wave 0-1
          
          // Combine with fade-in effect
          float fadeProgress = min(1.0f, elapsed / (float)(COLOR_CYCLE_TIME * 0.3)); // Fade in over first 30%
          
          int targetBrightness = Fconfig.touchedBrightness;
          faders[i].currentBrightness = (uint8_t)(targetBrightness * breatheValue * fadeProgress);
          
        } else {
          // Wave has passed, fade out with color shift to original color
          unsigned long fadeOutTime = elapsed - COLOR_CYCLE_TIME;
          const unsigned long FADE_OUT_DURATION = 250; // 250ms fade out
          
          if (fadeOutTime < FADE_OUT_DURATION) {
            animationComplete = false;
            
            // Fade to original color while dimming
            float fadeProgress = fadeOutTime / (float)FADE_OUT_DURATION;
            
            // Blend current color to original color
            uint8_t currentR = faders[i].red;
            uint8_t currentG = faders[i].green;
            uint8_t currentB = faders[i].blue;
            
            faders[i].red = (uint8_t)(currentR + (originalColors[i][0] - currentR) * fadeProgress);
            faders[i].green = (uint8_t)(currentG + (originalColors[i][1] - currentG) * fadeProgress);
            faders[i].blue = (uint8_t)(currentB + (originalColors[i][2] - currentB) * fadeProgress);
            
            // Fade brightness to base
            float brightnessFade = 1.0 - fadeProgress;
            faders[i].currentBrightness = (uint8_t)(Fconfig.touchedBrightness * brightnessFade + 
                                                   Fconfig.baseBrightness * fadeProgress);
          } else {
            // Completely finished - use original colors
            faders[i].red = originalColors[i][0];
            faders[i].green = originalColors[i][1];
            faders[i].blue = originalColors[i][2];
            faders[i].currentBrightness = Fconfig.baseBrightness;
          }
        }
      } else {
        // Haven't started yet
        animationComplete = false;
        faders[i].currentBrightness = 0;
        faders[i].red = 0;
        faders[i].green = 0;
        faders[i].blue = 0;
      }
    }
    
    // Update the pixels with the calculated colors and brightness
    for (int i = 0; i < NUM_FADERS; i++) {
      startupColors10[i][0] = faders[i].red;
      startupColors10[i][1] = faders[i].green;
      startupColors10[i][2] = faders[i].blue;

      uint32_t color = getScaledColor(faders[i]);
      for (int j = 0; j < PIXELS_PER_FADER; j++) {
        pixels.setPixelColor(i * PIXELS_PER_FADER + j, color);
      }
    }
    pixels.show();
    showExecutorStartupFromFaderColors(startupColors10);
    
    delay(10);
  }
  
  // Final cleanup - ensure all faders are properly reset
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].currentBrightness = Fconfig.baseBrightness;
    faders[i].red = originalColors[i][0];
    faders[i].green = originalColors[i][1];
    faders[i].blue = originalColors[i][2];
  }

  markKeyLedsDirty();
  updateKeyLeds();
}

void flashAllFadersRed() {
  // Store original colors
  uint8_t originalColors[NUM_FADERS][3];
  for (int i = 0; i < NUM_FADERS; i++) {
    originalColors[i][0] = faders[i].red;
    originalColors[i][1] = faders[i].green;
    originalColors[i][2] = faders[i].blue;
  }
  
  // Flash 5 times
  for (int flash = 0; flash < 5; flash++) {
    // Set all to red
    for (int i = 0; i < NUM_FADERS; i++) {
      // Scale red by touchedBrightness
      uint8_t scaledRed = (uint8_t)((255UL * Fconfig.touchedBrightness) / 255UL);
      for (int j = 0; j < PIXELS_PER_FADER; j++) {
        pixels.setPixelColor(i * PIXELS_PER_FADER + j, pixels.Color(scaledRed, 0, 0));
      }
    }
    pixels.show();
    delay(100);
    
    // Restore original colors and brightness via normal path
    for (int i = 0; i < NUM_FADERS; i++) {
      faders[i].red = originalColors[i][0];
      faders[i].green = originalColors[i][1];
      faders[i].blue = originalColors[i][2];
    }
    updateNeoPixels();
    delay(100);
  }
}
