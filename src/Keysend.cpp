#include <Arduino.h>
#include "Keysend.h"
#include <Keyboard.h>


// Using Teensy keyboard codes
const KeyMapping keyMap[] = {
    // Row 1 (101-110)
    {101, 'z', "Z"},
    {102, 'x', "X"},
    {103, 'c', "C"},
    {104, 'v', "V"},
    {105, 'b', "B"},
    {106, 'n', "N"},
    {107, 'm', "M"},
    {108, ',', "Comma"},
    {109, '.', "Period"},
    {110, '/', "Slash"},
    
    // Row 2 (201-210)
    {201, 'a', "A"},
    {202, 's', "S"},
    {203, 'd', "D"},
    {204, 'f', "F"},
    {205, 'g', "G"},
    {206, 'h', "H"},
    {207, 'j', "J"},
    {208, 'k', "K"},
    {209, 'l', "L"},
    {210, ';', "Semicolon"},
    
    // Row 3 (301-310)
    {301, 'q', "Q"},
    {302, 'w', "W"},
    {303, 'e', "E"},
    {304, 'r', "R"},
    {305, '=', "Equal"},
    {306, 'y', "Y"},
    {307, 'u', "U"},
    {308, 'i', "I"},
    {309, 'o', "O"},
    {310, 'p', "P"},
    
    // Row 4 (401-410)
    {401, '\'', "Apostrophe"},
    {402, ' ', "Space"},
    {403, KEY_TAB, "Tab"},
    {404, '`', "GraveAccent"},
    {405, KEY_LEFT_ARROW, "Left"},
    {406, KEY_RIGHT_ARROW, "Right"},
    {407, KEY_UP_ARROW, "Up"},
    {408, KEY_DOWN_ARROW, "Down"},
    {409, '\\', "Backslash"},
    {410, KEY_CAPS_LOCK, "CapsLock"}
};

const int KEY_MAP_SIZE = sizeof(keyMap) / sizeof(keyMap[0]);

const KeyMapping* getKeyMap() {
    return keyMap;
}

int getKeyMapSize() {
    return KEY_MAP_SIZE;
}

// Track key states for proper hold/release
struct KeyState {
    int executorIndex;
    bool isPressed;
    unsigned long lastUpdate;
};

// Array to track up to 40 keys
KeyState keyStates[40];
int activeKeyCount = 0;

// Initialize keyboard system
void initKeyboard() {
    Keyboard.begin();
    
    // Initialize key states
    for (int i = 0; i < 40; i++) {
        keyStates[i].executorIndex = 0;
        keyStates[i].isPressed = false;
        keyStates[i].lastUpdate = 0;
    }
}

// Find key code from executor index
int getKeyCode(int executorIndex) {
    for (int i = 0; i < KEY_MAP_SIZE; i++) {
        if (keyMap[i].executorIndex == executorIndex) {
            return keyMap[i].keyCode;
        }
    }
    return -1; // Not found
}

// Find or create key state entry
KeyState* getKeyState(int executorIndex) {
    // First, check if we already have a state for this key
    for (int i = 0; i < activeKeyCount; i++) {
        if (keyStates[i].executorIndex == executorIndex) {
            return &keyStates[i];
        }
    }
    
    // If not found and we have space, create new entry
    if (activeKeyCount < 40) {
        keyStates[activeKeyCount].executorIndex = executorIndex;
        keyStates[activeKeyCount].isPressed = false;
        keyStates[activeKeyCount].lastUpdate = millis();
        return &keyStates[activeKeyCount++];
    }
    
    return nullptr; // No space available
}

// Send key press
void sendKeyPress(const String& keyID) {
    int executorIndex = keyID.toInt();
    int keyCode = getKeyCode(executorIndex);
    
    if (keyCode == -1) {
        // Key not found in mapping
        return;
    }
    
    KeyState* state = getKeyState(executorIndex);
    if (state && !state->isPressed) {
        Keyboard.press(keyCode);
        state->isPressed = true;
        state->lastUpdate = millis();
    }
}

// Send key release
void sendKeyRelease(const String& keyID) {
    int executorIndex = keyID.toInt();
    int keyCode = getKeyCode(executorIndex);
    
    if (keyCode == -1) {
        // Key not found in mapping
        return;
    }
    
    KeyState* state = getKeyState(executorIndex);
    if (state && state->isPressed) {
        Keyboard.release(keyCode);
        state->isPressed = false;
        state->lastUpdate = millis();
    }
}

// Send key press and release (for tap/click)
void sendKeyTap(const String& keyID, unsigned long duration = 50) {
    sendKeyPress(keyID);
    delay(duration);
    sendKeyRelease(keyID);
}


// Release all keys (useful for cleanup or panic button)
void releaseAllKeys() {
    for (int i = 0; i < activeKeyCount; i++) {
        if (keyStates[i].isPressed) {
            int keyCode = getKeyCode(keyStates[i].executorIndex);
            if (keyCode != -1) {
                Keyboard.release(keyCode);
            }
            keyStates[i].isPressed = false;
        }
    }
    Keyboard.releaseAll(); // Extra safety
}
