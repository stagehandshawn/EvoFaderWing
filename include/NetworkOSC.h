// NetworkOSC.h
#ifndef NETWORK_OSC_H
#define NETWORK_OSC_H

#include <Arduino.h>
#include <QNEthernet.h>
#include <LiteOSCParser.h>
#include "Config.h"

using namespace qindesign::network;
using qindesign::osc::LiteOSCParser;

//================================
// GLOBAL NETWORK OBJECTS
//================================

extern EthernetUDP udp;

//================================
// FUNCTION DECLARATIONS
//================================

// Network setup and management
void setupNetwork();

// OSC message handling
void handleOscPacket(const char *address, int value);
void sendOscUpdate(Fader& f, int value, bool force = false);
void handleColorOsc(const char *address, const char *colorString);

void handleOscMovement(const char *address, int value);
void handleOscMessage();

void sendOscMessage(const char* address, const char* typeTag, const void* value);

// Page update
void handlePageUpdate(const char *address, int value);

// Color parsing (used by both NetworkOSC and NeoPixelControl)
void parseColorValues(const char *colorString, Fader& f);

// OSC utility functions
void printOSC(Print &out, const uint8_t *b, int len);
bool isBundleStart(const uint8_t *buf, size_t len);

#endif // NETWORK_OSC_H