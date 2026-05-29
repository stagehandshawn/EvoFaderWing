// NetworkOSC.h
#ifndef NETWORK_OSC_H
#define NETWORK_OSC_H

#include <Arduino.h>
#include <QNEthernet.h>
#include <LiteOSCParser.h>
#include "Config.h"

using namespace qindesign::network;
using qindesign::osc::LiteOSCParser;

class AsyncUDP;

//================================
// GLOBAL NETWORK OBJECTS
//================================

extern AsyncUDP oscUdp;
extern bool wingCmdMode;      // True when MA3 command line is active (add-to-cmdline mode)
extern bool wingCmdExecMode;  // True when MA3 command line has an auto-executing command
extern bool wingCmdCopySrc;   // True when copy/move source is already selected on MA3 cmdline
extern bool wingCmdThru;      // True when thru is active — send only exec number, no Page X.Y

//================================
// FUNCTION DECLARATIONS
//================================

// Network transport services (owned by NetworkManager)
void startNetworkServices();
void stopNetworkServices();
void restartNetworkServices();
void processOscQueue();  // Process queued OSC packets (call from loop)



// OSC message handling
void sendFaderOsc(Fader& f, int value, bool force = false);
void sendOscMessage(const char* address, const char* typeTag, const void* value);

// Page update
void handlePageUpdate(const char *address, int value);


// OSC utility functions
void printOSC(Print &out, const uint8_t *b, int len);
bool isBundleStart(const uint8_t *buf, size_t len);
void parseDualColorValues(const char *colorString, Fader& f);

#endif // NETWORK_OSC_H
