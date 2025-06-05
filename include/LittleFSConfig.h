#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <IPAddress.h>

#define NUM_FADERS 10
#define CONFIG_VERSION 1  // For future compatibility

struct FaderCalibration {
    int minVal;
    int maxVal;
};

struct FaderSettings {
    float pidKp;
    float pidKi;
    float pidKd;
    int motorDeadzone;
    int defaultPwm;
    int calibratePwm;
    int targetTolerance;
    int sendTolerance;
};

struct TouchSettings {
    uint8_t autoCalibrationMode;
    uint8_t touchThreshold;
    uint8_t releaseThreshold;
};

struct NetworkSettings {
    bool useDHCP;
    IPAddress staticIP;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress sendToIP;
    uint16_t receivePort;
    uint16_t sendPort;
};

class LittleFSConfig {
public:
    // Mount filesystem and optionally load config
    bool begin(bool autoLoad = true);

    // Configuration management
    bool loadConfig();
    bool saveConfig();
    bool resetToDefaults();
    bool createBackup();
    bool restoreBackup();
    
    // Individual section management (optional convenience functions)
    bool saveFaderSettings();
    bool saveNetworkSettings();
    bool saveTouchSettings();
    bool saveCalibration();
    
    // Validation
    bool validateConfig();
    void printConfig();
    
    // File management
    bool configExists();
    bool deleteConfig();
    size_t getConfigSize();

    // Configuration data with sensible defaults
    uint8_t fadeTime = 100;             // LED fade time in ms
    uint8_t baseBrightness = 60;        // Idle LED brightness
    uint8_t touchedBrightness = 120;    // Touch LED brightness

    FaderSettings faderSettings = {
        .pidKp = 1.00f,
        .pidKi = 0.00f,
        .pidKd = 0.00f,
        .motorDeadzone = 10,
        .defaultPwm = 255,
        .calibratePwm = 80,
        .targetTolerance = 2,
        .sendTolerance = 2
    };

    TouchSettings touchSettings = {
        .autoCalibrationMode = 2,
        .touchThreshold = 12,
        .releaseThreshold = 6
    };

    FaderCalibration faderCalibration[NUM_FADERS] = {};  // Initialize to zeros

    NetworkSettings network = {
        .useDHCP = true,
        .staticIP = IPAddress(192, 168, 0, 169),
        .gateway = IPAddress(192, 168, 0, 1),
        .subnet = IPAddress(255, 255, 255, 0),
        .sendToIP = IPAddress(192, 168, 0, 100),
        .receivePort = 8000,
        .sendPort = 9000
    };

private:
    const char* configPath = "/config.json";
    const char* backupPath = "/config_backup.json";
    bool filesystemMounted = false;
    
    // Helper functions
    void setDefaults();
    bool validateNetworkSettings();
    bool validateFaderSettings();
    bool validateTouchSettings();
    void printError(const char* operation, const char* details = nullptr);
};