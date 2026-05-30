// KeyCmdHandler.cpp - Key index mapping and OSC/CMD key dispatch

#include "KeyCmdHandler.h"
#include "Config.h"
#include "EEPROMStorage.h"
#include "ExecutorStatus.h"
#include "NetworkOSC.h"
#include "Keysend.h"
#include "Utils.h"

#define I2C_DEBUG_PRINTF(...) I2C_BUS_DEBUG_PRINTF(__VA_ARGS__)
#define I2C_ERROR_PRINTF(...) I2C_BUS_ERROR_PRINTF(__VA_ARGS__)

// ---------------------------------------------------------------------------
// Key number ↔ executor index mapping
// ---------------------------------------------------------------------------

int keyIndexFromNumber(uint16_t keyNumber) {
  if (keyNumber >= 101 && keyNumber <= 110) return keyNumber - 101;
  if (keyNumber >= 201 && keyNumber <= 210) return 10 + (keyNumber - 201);
  if (keyNumber >= 301 && keyNumber <= 310) return 20 + (keyNumber - 301);
  if (keyNumber >= 401 && keyNumber <= 410) return 30 + (keyNumber - 401);
  return -1;
}

uint16_t keyNumberFromIndex(int index) {
  if (index >= 0  && index < 10) return 101 + index;
  if (index >= 10 && index < 20) return 201 + (index - 10);
  if (index >= 20 && index < 30) return 301 + (index - 20);
  if (index >= 30 && index < 40) return 401 + (index - 30);
  return 0;
}

// ---------------------------------------------------------------------------
// sendKeyOSC
// ---------------------------------------------------------------------------
// Dispatches a key press (state=1) or release (state=0).
//
// CMD-mode intercept: when any wing CMD flag is active on a press, a temporary
// "EvoFaderWingCMD" macro is built and fired via /cmd OSC instead of sending
// the raw key event.
//
// Wing status → behaviour matrix:
// ┌──────────────────────────────────────┬──────────────────────┬─────────┐
// │ State                                │ Macro command        │ Execute │
// ├──────────────────────────────────────┼──────────────────────┼─────────┤
// │ CMD_MODE   (most keywords)           │ Page X.Y             │ No      │
// │ CMD_EXEC_MODE (store, del, ...)      │ Page X.Y             │ Yes     │
// │ CMD_COPY_SRC + empty target          │ At Page X.Y          │ Yes     │
// │ CMD_COPY_SRC + occupied target       │ + Page X.Y           │ No      │
// │ CMD_THRU (thru open, no end)         │ Y   (exec# only)     │ No      │
// │ CMD_THRU + CMD_EXEC_MODE (thru done) │ Y   (exec# only)     │ Yes     │
// └──────────────────────────────────────┴──────────────────────┴─────────┘

void sendKeyOSC(uint16_t keyNumber, uint8_t state) {
  // Validate key number
  if (!((keyNumber >= 101 && keyNumber <= 110) ||
        (keyNumber >= 201 && keyNumber <= 210) ||
        (keyNumber >= 301 && keyNumber <= 310) ||
        (keyNumber >= 401 && keyNumber <= 410))) {
    I2C_ERROR_PRINTF("[OSC] Invalid key number for OSC: %d", keyNumber);
    return;
  }

  // Validate state
  if (state > 1) {
    I2C_ERROR_PRINTF("[OSC] Invalid key state: %d", state);
    return;
  }

  // Desk lock: block all key/OSC output silently
  if (deskLocked) {
    return;
  }

  // CMD mode intercept
  if (state == 1 && (wingCmdMode || wingCmdExecMode || wingCmdCopySrc || wingCmdThru)) {
    bool targetOccupied = false;
    if (wingCmdCopySrc) {
      int targetIdx = keyIndexFromNumber(keyNumber);
      targetOccupied = (targetIdx >= 0 && executorStatus[targetIdx] > 0);
    }

    const char* executeFlag = (wingCmdExecMode || (wingCmdCopySrc && !targetOccupied)) ? "Yes" : "No";
    char deleteBuf[40];
    char storeMacroBuf[40];
    char storeLineBuf[40];
    char setCmdBuf[80];
    char setAddBuf[56];
    char setExecBuf[56];
    char goBuf[32];

    snprintf(deleteBuf,     sizeof(deleteBuf),     "Delete Macro EvoFaderWingCMD /NoOops");
    snprintf(storeMacroBuf, sizeof(storeMacroBuf), "Store Macro EvoFaderWingCMD /NoOops");
    snprintf(storeLineBuf,  sizeof(storeLineBuf),  "Store Macro EvoFaderWingCMD.1 /NoOops");

    if (wingCmdThru) {
      snprintf(setCmdBuf, sizeof(setCmdBuf), "Set Macro EvoFaderWingCMD.1 command=\"%d\" /NoOops", (int)keyNumber);
    } else if (wingCmdCopySrc && targetOccupied) {
      snprintf(setCmdBuf, sizeof(setCmdBuf), "Set Macro EvoFaderWingCMD.1 command=\"+ Page %d.%d\" /NoOops", currentOSCPage, (int)keyNumber);
    } else if (wingCmdCopySrc) {
      snprintf(setCmdBuf, sizeof(setCmdBuf), "Set Macro EvoFaderWingCMD.1 command=\"At Page %d.%d\" /NoOops", currentOSCPage, (int)keyNumber);
    } else {
      snprintf(setCmdBuf, sizeof(setCmdBuf), "Set Macro EvoFaderWingCMD.1 command=\"Page %d.%d\" /NoOops", currentOSCPage, (int)keyNumber);
    }

    snprintf(setAddBuf,  sizeof(setAddBuf),  "Set Macro EvoFaderWingCMD.1 AddToCmdLine=\"Yes\" /NoOops");
    snprintf(setExecBuf, sizeof(setExecBuf), "Set Macro EvoFaderWingCMD.1 Execute=\"%s\" /NoOops", executeFlag);
    snprintf(goBuf,      sizeof(goBuf),      "Go Macro EvoFaderWingCMD");

    sendOscMessage("/cmd", ",s", deleteBuf);
    delay(10);
    sendOscMessage("/cmd", ",s", storeMacroBuf);
    sendOscMessage("/cmd", ",s", storeLineBuf);
    sendOscMessage("/cmd", ",s", setCmdBuf);
    sendOscMessage("/cmd", ",s", setAddBuf);
    sendOscMessage("/cmd", ",s", setExecBuf);
    delay(50);
    sendOscMessage("/cmd", ",s", goBuf);

    I2C_DEBUG_PRINTF("[CMD] mode=%s key=%d occupied=%d macro fired",
      wingCmdThru ? "thru" : (wingCmdCopySrc ? "copySrc" : (wingCmdExecMode ? "exec" : "cmd")),
      (int)keyNumber, targetOccupied ? 1 : 0);
    return;
  }

  // Normal dispatch: keystroke or OSC
  if (Fconfig.sendKeystrokes) {
    state ? sendKeyPress(keyNumber) : sendKeyRelease(keyNumber);
    I2C_DEBUG_PRINTF("[Key] Sent: %d %s", keyNumber, state ? "PRESSED" : "RELEASED");
  } else {
    char oscAddress[32];
    snprintf(oscAddress, sizeof(oscAddress), "/Key%d", keyNumber);
    int keyState = (int)state;
    sendOscMessage(oscAddress, ",i", &keyState);
    I2C_DEBUG_PRINTF("[OSC] Sent: %s %d (key %d %s)",
      oscAddress, keyState, keyNumber, state ? "PRESSED" : "RELEASED");
  }
}
