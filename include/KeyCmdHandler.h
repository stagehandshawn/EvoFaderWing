// KeyCmdHandler.h - Key index mapping and OSC/CMD key dispatch
#ifndef KEY_CMD_HANDLER_H
#define KEY_CMD_HANDLER_H

#include <Arduino.h>

// Maps a key number (e.g. 101-110, 201-210, ...) to a flat executor index 0-39.
// Returns -1 for unknown key numbers.
int      keyIndexFromNumber(uint16_t keyNumber);

// Inverse of keyIndexFromNumber. Returns 0 for out-of-range indices.
uint16_t keyNumberFromIndex(int index);

// Dispatches a key press/release: intercepts CMD-mode keys and routes all
// others to OSC or USB keystroke output depending on Fconfig.sendKeystrokes.
void sendKeyOSC(uint16_t keyNumber, uint8_t state);

#endif  // KEY_CMD_HANDLER_H
