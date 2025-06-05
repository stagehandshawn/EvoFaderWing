#include "LittleFSConfig.h"

static LittleFS_QSPIFlash flashFS;

bool LittleFSConfig::begin(bool autoLoad) {
    if (!flashFS.begin()) {
        printError("mount", "Failed to mount LittleFS_QSPIFlash");
        return false;
    }
    
    filesystemMounted = true;
    Serial.println("LittleFS mounted successfully");
    
    // Set defaults first
    setDefaults();
    
    // Optionally load existing config
    if (autoLoad) {
        if (!loadConfig()) {
            Serial.println("No valid config found, using defaults");
            // Save defaults for next time
            saveConfig();
        }
    }
    
    return true;
}

bool LittleFSConfig::loadConfig() {
    if (!filesystemMounted) {
        printError("load", "Filesystem not mounted");
        return false;
    }
    
    File file = flashFS.open(configPath, FILE_READ);
    if (!file) {
        printError("load", "Config file not found");
        return false;
    }

    StaticJsonDocument<3072> doc;  // Increased size for safety
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        printError("load", ("JSON parse error: " + String(error.c_str())).c_str());
        return false;
    }

    // Check version for future compatibility
    int version = doc["version"] | 0;
    if (version > CONFIG_VERSION) {
        printError("load", "Config version too new");
        return false;
    }

    // Load brightness settings
    fadeTime = doc["fadeTime"] | fadeTime;
    baseBrightness = doc["baseBrightness"] | baseBrightness;
    touchedBrightness = doc["touchedBrightness"] | touchedBrightness;

    // Load fader settings
    JsonObject fader = doc["fader"];
    faderSettings.pidKp = fader["kp"] | faderSettings.pidKp;
    faderSettings.pidKi = fader["ki"] | faderSettings.pidKi;
    faderSettings.pidKd = fader["kd"] | faderSettings.pidKd;
    faderSettings.motorDeadzone = fader["deadzone"] | faderSettings.motorDeadzone;
    faderSettings.defaultPwm = fader["defaultPwm"] | faderSettings.defaultPwm;
    faderSettings.calibratePwm = fader["calibratePwm"] | faderSettings.calibratePwm;
    faderSettings.targetTolerance = fader["targetTolerance"] | faderSettings.targetTolerance;
    faderSettings.sendTolerance = fader["sendTolerance"] | faderSettings.sendTolerance;

    // Load touch settings
    JsonObject touch = doc["touch"];
    touchSettings.autoCalibrationMode = touch["mode"] | touchSettings.autoCalibrationMode;
    touchSettings.touchThreshold = touch["touch"] | touchSettings.touchThreshold;
    touchSettings.releaseThreshold = touch["release"] | touchSettings.releaseThreshold;

    // Load calibration array
    JsonArray cal = doc["calibration"];
    for (int i = 0; i < NUM_FADERS && i < cal.size(); i++) {
        faderCalibration[i].minVal = cal[i]["min"] | 0;
        faderCalibration[i].maxVal = cal[i]["max"] | 1023;
    }

    // Load network settings
    JsonObject net = doc["network"];
    network.useDHCP = net["dhcp"] | network.useDHCP;

    // Load IP addresses as arrays
    JsonArray staticIP = net["staticIP"];
    JsonArray gateway = net["gateway"];
    JsonArray subnet = net["subnet"];
    JsonArray sendToIP = net["sendToIP"];
    
    if (staticIP.size() >= 4) {
        for (int i = 0; i < 4; i++) {
            network.staticIP[i] = staticIP[i] | network.staticIP[i];
        }
    }
    
    if (gateway.size() >= 4) {
        for (int i = 0; i < 4; i++) {
            network.gateway[i] = gateway[i] | network.gateway[i];
        }
    }
    
    if (subnet.size() >= 4) {
        for (int i = 0; i < 4; i++) {
            network.subnet[i] = subnet[i] | network.subnet[i];
        }
    }
    
    if (sendToIP.size() >= 4) {
        for (int i = 0; i < 4; i++) {
            network.sendToIP[i] = sendToIP[i] | network.sendToIP[i];
        }
    }

    network.receivePort = net["receivePort"] | network.receivePort;
    network.sendPort = net["sendPort"] | network.sendPort;

    // Validate loaded config
    if (!validateConfig()) {
        printError("load", "Loaded config failed validation");
        return false;
    }

    Serial.println("Config loaded successfully from LittleFS");
    return true;
}

bool LittleFSConfig::saveConfig() {
    if (!filesystemMounted) {
        printError("save", "Filesystem not mounted");
        return false;
    }
    
    // Validate before saving
    if (!validateConfig()) {
        printError("save", "Config validation failed");
        return false;
    }

    File file = flashFS.open(configPath, FILE_WRITE);
    if (!file) {
        printError("save", "Failed to open config file for writing");
        return false;
    }

    StaticJsonDocument<3072> doc;

    // Save version for future compatibility
    doc["version"] = CONFIG_VERSION;

    // Save brightness settings
    doc["fadeTime"] = fadeTime;
    doc["baseBrightness"] = baseBrightness;
    doc["touchedBrightness"] = touchedBrightness;

    // Save fader settings
    JsonObject fader = doc.createNestedObject("fader");
    fader["kp"] = faderSettings.pidKp;
    fader["ki"] = faderSettings.pidKi;
    fader["kd"] = faderSettings.pidKd;
    fader["deadzone"] = faderSettings.motorDeadzone;
    fader["defaultPwm"] = faderSettings.defaultPwm;
    fader["calibratePwm"] = faderSettings.calibratePwm;
    fader["targetTolerance"] = faderSettings.targetTolerance;
    fader["sendTolerance"] = faderSettings.sendTolerance;

    // Save touch settings
    JsonObject touch = doc.createNestedObject("touch");
    touch["mode"] = touchSettings.autoCalibrationMode;
    touch["touch"] = touchSettings.touchThreshold;
    touch["release"] = touchSettings.releaseThreshold;

    // Save calibration array
    JsonArray cal = doc.createNestedArray("calibration");
    for (int i = 0; i < NUM_FADERS; i++) {
        JsonObject item = cal.createNestedObject();
        item["min"] = faderCalibration[i].minVal;
        item["max"] = faderCalibration[i].maxVal;
    }

    // Save network settings
    JsonObject net = doc.createNestedObject("network");
    net["dhcp"] = network.useDHCP;

    JsonArray ip = net.createNestedArray("staticIP");
    JsonArray gw = net.createNestedArray("gateway");
    JsonArray sn = net.createNestedArray("subnet");
    JsonArray to = net.createNestedArray("sendToIP");

    for (int i = 0; i < 4; i++) {
        ip.add(network.staticIP[i]);
        gw.add(network.gateway[i]);
        sn.add(network.subnet[i]);
        to.add(network.sendToIP[i]);
    }

    net["receivePort"] = network.receivePort;
    net["sendPort"] = network.sendPort;

    // Write to file
    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    if (bytesWritten == 0) {
        printError("save", "Failed to write config data");
        return false;
    }

    Serial.printf("Config saved to LittleFS (%d bytes)\n", bytesWritten);
    return true;
}

bool LittleFSConfig::resetToDefaults() {
    Serial.println("Resetting configuration to defaults...");
    setDefaults();
    return saveConfig();
}

bool LittleFSConfig::createBackup() {
    if (!filesystemMounted || !configExists()) {
        return false;
    }
    
    // Copy config to backup
    File source = flashFS.open(configPath, FILE_READ);
    File backup = flashFS.open(backupPath, FILE_WRITE);
    
    if (!source || !backup) {
        if (source) source.close();
        if (backup) backup.close();
        return false;
    }
    
    // Copy file contents
    while (source.available()) {
        backup.write(source.read());
    }
    
    source.close();
    backup.close();
    
    Serial.println("Config backup created");
    return true;
}

bool LittleFSConfig::restoreBackup() {
    if (!filesystemMounted) {
        return false;
    }
    
    File backup = flashFS.open(backupPath, FILE_READ);
    if (!backup) {
        printError("restore", "No backup file found");
        return false;
    }
    backup.close();
    
    // Delete current config and rename backup
    flashFS.remove(configPath);
    
    File source = flashFS.open(backupPath, FILE_READ);
    File dest = flashFS.open(configPath, FILE_WRITE);
    
    if (!source || !dest) {
        if (source) source.close();
        if (dest) dest.close();
        return false;
    }
    
    // Copy backup to main config
    while (source.available()) {
        dest.write(source.read());
    }
    
    source.close();
    dest.close();
    
    // Reload config
    bool success = loadConfig();
    if (success) {
        Serial.println("Config restored from backup");
    }
    
    return success;
}

bool LittleFSConfig::validateConfig() {
    return validateNetworkSettings() && 
           validateFaderSettings() && 
           validateTouchSettings();
}

bool LittleFSConfig::validateNetworkSettings() {
    // Validate port ranges
    if (network.receivePort < 1024 || network.receivePort > 65535) {
        Serial.printf("Invalid receive port: %d\n", network.receivePort);
        return false;
    }
    
    if (network.sendPort < 1024 || network.sendPort > 65535) {
        Serial.printf("Invalid send port: %d\n", network.sendPort);
        return false;
    }
    
    return true;
}

bool LittleFSConfig::validateFaderSettings() {
    // Validate PWM values
    if (faderSettings.defaultPwm < 0 || faderSettings.defaultPwm > 255) {
        Serial.printf("Invalid default PWM: %d\n", faderSettings.defaultPwm);
        return false;
    }
    
    if (faderSettings.calibratePwm < 0 || faderSettings.calibratePwm > 255) {
        Serial.printf("Invalid calibrate PWM: %d\n", faderSettings.calibratePwm);
        return false;
    }
    
    // Validate tolerances
    if (faderSettings.targetTolerance < 1 || faderSettings.targetTolerance > 50) {
        Serial.printf("Invalid target tolerance: %d\n", faderSettings.targetTolerance);
        return false;
    }
    
    return true;
}

bool LittleFSConfig::validateTouchSettings() {
    // Validate touch thresholds
    if (touchSettings.touchThreshold < 1 || touchSettings.touchThreshold > 255) {
        Serial.printf("Invalid touch threshold: %d\n", touchSettings.touchThreshold);
        return false;
    }
    
    if (touchSettings.releaseThreshold < 1 || touchSettings.releaseThreshold > 255) {
        Serial.printf("Invalid release threshold: %d\n", touchSettings.releaseThreshold);
        return false;
    }
    
    return true;
}

void LittleFSConfig::printConfig() {
    Serial.println("=== Current Configuration ===");
    Serial.printf("Fade Time: %d ms\n", fadeTime);
    Serial.printf("Base Brightness: %d\n", baseBrightness);
    Serial.printf("Touched Brightness: %d\n", touchedBrightness);
    
    Serial.println("\nFader Settings:");
    Serial.printf("  PID: Kp=%.2f, Ki=%.2f, Kd=%.2f\n", 
                  faderSettings.pidKp, faderSettings.pidKi, faderSettings.pidKd);
    Serial.printf("  PWM: Default=%d, Calibrate=%d, Deadzone=%d\n",
                  faderSettings.defaultPwm, faderSettings.calibratePwm, faderSettings.motorDeadzone);
    
    Serial.println("\nTouch Settings:");
    Serial.printf("  Mode: %d, Touch: %d, Release: %d\n",
                  touchSettings.autoCalibrationMode, touchSettings.touchThreshold, touchSettings.releaseThreshold);
    
    Serial.println("\nNetwork Settings:");
    Serial.printf("  DHCP: %s\n", network.useDHCP ? "Yes" : "No");
    Serial.printf("  Static IP: %d.%d.%d.%d\n", network.staticIP[0], network.staticIP[1], network.staticIP[2], network.staticIP[3]);
    Serial.printf("  Gateway: %d.%d.%d.%d\n", network.gateway[0], network.gateway[1], network.gateway[2], network.gateway[3]);
    Serial.printf("  Send To: %d.%d.%d.%d\n", network.sendToIP[0], network.sendToIP[1], network.sendToIP[2], network.sendToIP[3]);
    Serial.printf("  Ports: RX=%d, TX=%d\n", network.receivePort, network.sendPort);
    
    Serial.println("=============================");
}

// Convenience functions for individual saves
bool LittleFSConfig::saveFaderSettings() { return saveConfig(); }
bool LittleFSConfig::saveNetworkSettings() { return saveConfig(); }
bool LittleFSConfig::saveTouchSettings() { return saveConfig(); }
bool LittleFSConfig::saveCalibration() { return saveConfig(); }

// File management functions
bool LittleFSConfig::configExists() {
    return filesystemMounted && flashFS.exists(configPath);
}

bool LittleFSConfig::deleteConfig() {
    if (!filesystemMounted) return false;
    return flashFS.remove(configPath);
}

size_t LittleFSConfig::getConfigSize() {
    if (!configExists()) return 0;
    
    File file = flashFS.open(configPath, FILE_READ);
    size_t size = file.size();
    file.close();
    return size;
}

// Private helper functions
void LittleFSConfig::setDefaults() {
    // Initialize calibration array with defaults
    for (int i = 0; i < NUM_FADERS; i++) {
        faderCalibration[i].minVal = 0;
        faderCalibration[i].maxVal = 1023;
    }
    
    // Other defaults are set in the struct initializers
}

void LittleFSConfig::printError(const char* operation, const char* details) {
    Serial.printf("LittleFS %s error", operation);
    if (details) {
        Serial.printf(": %s", details);
    }
    Serial.println();
}