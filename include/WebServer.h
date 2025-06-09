#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <QNEthernet.h>
#include "Config.h"

using namespace qindesign::network;

//================================
// GLOBAL WEB SERVER OBJECTS
//================================

extern EthernetUDP udp;  // Also declare udp for consistency
extern EthernetServer server;
extern EthernetClient client;


//================================
// FUNCTION DECLARATIONS
//================================

// New OSC settings handler
void handleOSCSettings(String request);

// Validation functions
bool isValidIP(IPAddress ip);
bool isValidPort(int port);
void sendErrorResponse(const char* errorMsg);

// Server management
void startWebServer();
void pollWebServer();
void handleWebServer();

// Request handlers
void handleRoot();
void handleNetworkSettings(String request);
void handleCalibrationSettings(String request);
void handlePIDSettings(String request);
void handleFaderSettings(String request);
void handleTouchSettings(String request);
void handleRunCalibration();
void handleDebugToggle(String requestBody);
void handleResetDefaults();
void handleNetworkReset();
void handleStatsPage();

// Response helpers
void send404Response();
void sendRedirect();

#endif // WEB_SERVER_H