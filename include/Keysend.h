// Keysend.h
#ifndef KEYSEND
#define KEYSEND

#include <Arduino.h>
#include <Keyboard.h>

struct KeyMapping {
    int executorIndex;
    int keyCode;
    const char* keyName;
};

void initKeyboard();
void sendKeyPress(const String& keyID);
void sendKeyRelease(const String& keyID);
void releaseAllKeys();
const KeyMapping* getKeyMap();
int getKeyMapSize();

#endif
