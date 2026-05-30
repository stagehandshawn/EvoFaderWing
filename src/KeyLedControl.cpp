// KeyLedControl.cpp

#include "KeyLedControl.h"
#include "ExecutorStatus.h"
#include "Utils.h"
#include <Adafruit_NeoPixel.h>

// Separate strip for the executor keys
Adafruit_NeoPixel keyPixels(EXECUTOR_LED_COUNT, EXECUTOR_LED_PIN, NEO_RGB + NEO_KHZ800);

static bool keyLedsDirty = false;

// Physical strip order for the exec keys (serpentine):
// 401-410, 310-301, 201-210, 110-101
// Map to absolute start pixel indices so gaps can be ignored by
// editing the start values directly (EXECUTOR_LED_COUNT must be total including gap pixels).
static const uint16_t EXEC_LED_START[NUM_EXECUTORS_TRACKED] = {
  // 101-110 (bottom row, reversed)
  78, 76, 74, 72, 70, 68, 66, 64, 62, 60,
  // 201-210
  40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
  // 301-310 (reversed)
  38, 36, 34, 32, 32, 32, 32, 32, 32, 32,
  // 401-410 (top row)
  0, 3, 6, 9, 13, 18, 21, 24, 27, 30
};

static uint32_t buildExecColor(int execIndex, uint8_t brightness) {
  if (brightness == 0) {
    return keyPixels.Color(0, 0, 0);
  }

  if (!execConfig.useStaticColor) {
    uint8_t baseR = executorColors[execIndex][0];
    uint8_t baseG = executorColors[execIndex][1];
    uint8_t baseB = executorColors[execIndex][2];

    // If no color received yet, default to white scaled by brightness
    if (baseR == 0 && baseG == 0 && baseB == 0) {
      return keyPixels.Color(brightness, brightness, brightness);
    }

    uint8_t r = (uint16_t)baseR * brightness / 255;
    uint8_t g = (uint16_t)baseG * brightness / 255;
    uint8_t b = (uint16_t)baseB * brightness / 255;
    return keyPixels.Color(r, g, b);
  }

  uint8_t r = (uint16_t)execConfig.staticRed * brightness / 255;
  uint8_t g = (uint16_t)execConfig.staticGreen * brightness / 255;
  uint8_t b = (uint16_t)execConfig.staticBlue * brightness / 255;
  return keyPixels.Color(r, g, b);
}

static void fillExecutorPixels(int execIndex, uint8_t brightness) {
  if (execIndex < 0 || execIndex >= NUM_EXECUTORS_TRACKED) {
    return;
  }

  uint32_t color = buildExecColor(execIndex, brightness);
  int startPixel = EXEC_LED_START[execIndex];
  if (startPixel < 0 || startPixel + EXECUTOR_PIXELS_PER_KEY > EXECUTOR_LED_COUNT) {
    return;
  }

  for (int i = 0; i < EXECUTOR_PIXELS_PER_KEY; ++i) {
    keyPixels.setPixelColor(startPixel + i, color);
  }
}

void setupKeyLeds() {
  keyPixels.begin();
  keyPixels.clear();

  // Start dark; they'll light when we learn populated/off/on status
  keyPixels.show();

  LED_EXEC_DEBUG_PRINTF("Key LED strip ready on pin %d with %d pixels", EXECUTOR_LED_PIN, EXECUTOR_LED_COUNT);
  
}

void markKeyLedsDirty() {
  keyLedsDirty = true;
}

void updateKeyLeds() {
  if (!keyLedsDirty) {
    return;
  }

  keyLedsDirty = false;

  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    uint8_t status = executorStatus[i]; // 0=empty,1=populated off,2=on
    uint8_t brightness = 0;

    if (status == 2) {
      brightness = execConfig.activeBrightness;
    } else if (status == 1) {
      brightness = execConfig.useMA3OccupiedBrightness ? execMaBrightness : execConfig.baseBrightness;
    } else {
      brightness = 0;
    }

    fillExecutorPixels(i, brightness);
  }

  keyPixels.show();
}

void showExecutorStartupFromFaderColors(const uint8_t colors10[NUM_FADERS][3]) {
  if (colors10 == nullptr) {
    return;
  }

  // Repeat the 10 startup colors across the 40 executor LEDs in groups of 10.
  for (int execIndex = 0; execIndex < NUM_EXECUTORS_TRACKED; ++execIndex) {
    int faderIndex = execIndex % NUM_FADERS;
    uint8_t r = colors10[faderIndex][0];
    uint8_t g = colors10[faderIndex][1];
    uint8_t b = colors10[faderIndex][2];
    uint32_t color = keyPixels.Color(r, g, b);

    int startPixel = EXEC_LED_START[execIndex];
    if (startPixel < 0 || startPixel + EXECUTOR_PIXELS_PER_KEY > EXECUTOR_LED_COUNT) {
      continue;
    }

    for (int i = 0; i < EXECUTOR_PIXELS_PER_KEY; ++i) {
      keyPixels.setPixelColor(startPixel + i, color);
    }
  }

  keyPixels.show();
}
