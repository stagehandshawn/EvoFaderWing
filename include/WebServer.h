// WebServer.h
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <QNEthernet.h>
#include "Config.h"

using namespace qindesign::network;

//================================
// GLOBAL WEB SERVER OBJECTS
//================================

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
void handleNetworkSettings(String request);
void handleCalibrationSettings(String request);
void handleFaderSettings(String request);
void handleLEDSettingsSave(String request);
void handleTouchSettings(String request);
void handleRunCalibration();
void handleEnableDisabledFaders();
void handleDebugToggle(String requestBody);
void handleDebugSettings(String request);
void handleResetDefaults();
void handleNetworkReset();

// Html building functions
void handleRoot();
void handleStatsPage();
void handleStatsData();
void handleFaderSettingsPage();
void handleLEDSettingsPage();
void handleOSCSettingsPage();
void handleDebugSettingsPage();

void sendCommonStyles();
void sendNavigationHeader(const char* pageTitle);

void handleGMA3ShortcutsDownload();
void handleLuaDownload();
void handleFavicon();

void waitForWriteSpace(size_t minBytes = 100);
void sendMessagePage(const char* title, const char* message, const char* redirectUrl = nullptr, int redirectSeconds = 0);
void handleRebootRequest();
// Response helpers
void send404Response();
void sendRedirect();
void sendFooter();

#endif // WEB_SERVER_H
