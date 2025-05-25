// === OLED.cpp ===
// Simple OLED wrapper using Adafruit SSD1306 library
// Designed for Teensy 4.1 fader wing project
// Provides easy variable display functions for your 128x64 SSD1306 display

#include "OLED.h"
#include <stdarg.h>
#include <stdio.h>
#include <IPAddress.h>

// === Constructor and Destructor ===

OLED::OLED() {
    oledDisplay = nullptr;
    i2cAddress = 0;
    displayInitialized = false;
}

OLED::~OLED() {
    if (oledDisplay) {
        delete oledDisplay;
        oledDisplay = nullptr;
    }
}

// === Public Initialization Functions ===

bool OLED::begin() {
    // Try to auto-detect the display address by testing common addresses
    debugPrint("[OLED] Auto-detecting display address...");
    
    // Test primary address first (most common for your blue/yellow displays)
    if (testAddress(OLED_ADDR_PRIMARY)) {
        i2cAddress = OLED_ADDR_PRIMARY;
        debugPrintf("[OLED] Display found at primary address 0x%02X", i2cAddress);
    }
    // Test secondary address if primary failed
    else if (testAddress(OLED_ADDR_SECONDARY)) {
        i2cAddress = OLED_ADDR_SECONDARY;
        debugPrintf("[OLED] Display found at secondary address 0x%02X", i2cAddress);
    }
    else {
        debugPrint("[OLED] ERROR: No display found at any address");
        return false;
    }
    
    // Create Adafruit display object with detected address
    oledDisplay = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
    
    // Initialize the display using Adafruit library
    if (!oledDisplay->begin(SSD1306_SWITCHCAPVCC, i2cAddress)) {
        debugPrint("[OLED] ERROR: SSD1306 allocation failed");
        delete oledDisplay;
        oledDisplay = nullptr;
        return false;
    }
    
    // Configure display settings
    oledDisplay->clearDisplay();
    oledDisplay->setTextSize(TEXT_SIZE_SMALL);      // Small text by default
    oledDisplay->setTextColor(SSD1306_WHITE);       // White text
    oledDisplay->cp437(true);                       // Use full 256 character 'Code Page 437' font
    
    displayInitialized = true;
    debugPrint("[OLED] Display initialized successfully using Adafruit library");
    return true;
}

bool OLED::begin(uint8_t address) {
    // Initialize with user-specified address
    debugPrintf("[OLED] Initializing display at specified address 0x%02X", address);
    
    if (testAddress(address)) {
        i2cAddress = address;
        
        // Create Adafruit display object
        oledDisplay = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
        
        // Initialize the display using Adafruit library
        if (!oledDisplay->begin(SSD1306_SWITCHCAPVCC, i2cAddress)) {
            debugPrint("[OLED] ERROR: SSD1306 allocation failed");
            delete oledDisplay;
            oledDisplay = nullptr;
            return false;
        }
        
        // Configure display settings
        oledDisplay->clearDisplay();
        oledDisplay->setTextSize(TEXT_SIZE_SMALL);
        oledDisplay->setTextColor(SSD1306_WHITE);
        oledDisplay->cp437(true);
        
        displayInitialized = true;
        debugPrint("[OLED] Display initialized successfully");
        return true;
    } else {
        debugPrintf("[OLED] ERROR: No display found at address 0x%02X", address);
        return false;
    }
}

bool OLED::isInitialized() {
    return displayInitialized;
}

uint8_t OLED::getAddress() {
    return i2cAddress;
}

// === Public Display Control Functions ===

void OLED::clear() {
    // Clear display buffer
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->clearDisplay();
}

void OLED::display() {
    // Update physical display with buffer contents
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->display();
}

void OLED::setBrightness(uint8_t brightness) {
    // Set display contrast/brightness (0-255)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->ssd1306_command(SSD1306_SETCONTRAST);
    oledDisplay->ssd1306_command(brightness);
    debugPrintf("[OLED] Brightness set to %d", brightness);
}

void OLED::setInverted(bool inverted) {
    // Set normal or inverted display mode
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->invertDisplay(inverted);
    debugPrintf("[OLED] Display mode: %s", inverted ? "INVERTED" : "NORMAL");
}

void OLED::powerOff() {
    // Turn display off (sleep mode)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->ssd1306_command(SSD1306_DISPLAYOFF);
    debugPrint("[OLED] Display powered off");
}

void OLED::powerOn() {
    // Turn display on (wake from sleep)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->ssd1306_command(SSD1306_DISPLAYON);
    debugPrint("[OLED] Display powered on");
}

// === Public Text Functions ===

void OLED::setCursor(uint8_t x, uint8_t y) {
    // Set cursor position in pixels
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->setCursor(x, y);
}

void OLED::setTextSize(uint8_t size) {
    // Set text size (1, 2, or 3)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->setTextSize(size);
}

void OLED::setTextColor(uint16_t color) {
    // Set text color (SSD1306_WHITE, SSD1306_BLACK, SSD1306_INVERSE)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->setTextColor(color);
}

void OLED::print(const char* text) {
    // Print text string at current cursor position
    if (!displayInitialized || !oledDisplay || !text) return;
    oledDisplay->print(text);
}

void OLED::println(const char* text) {
    // Print text string with automatic newline
    if (!displayInitialized || !oledDisplay || !text) return;
    oledDisplay->println(text);
}

void OLED::printf(const char* format, ...) {
    // Print formatted text (similar to sprintf)
    if (!displayInitialized || !oledDisplay || !format) return;
    
    char buffer[128];  // Buffer for formatted text
    
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    oledDisplay->print(buffer);
}

// === Public Variable Display Functions ===

void OLED::showInt(const char* label, int value, uint8_t line) {
    // Display integer variable on specified line (0-7 for small text)
    if (!displayInitialized || !oledDisplay) return;
    
    clearLine(line);
    setCursor(0, line * CHAR_HEIGHT_SMALL);
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    printf("%s: %d", label, value);
}

void OLED::showFloat(const char* label, float value, uint8_t line) {
    // Display float variable on specified line (2 decimal places)
    if (!displayInitialized || !oledDisplay) return;
    
    clearLine(line);
    setCursor(0, line * CHAR_HEIGHT_SMALL);
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    printf("%s: %.2f", label, value);
}

void OLED::showBool(const char* label, bool value, uint8_t line) {
    // Display boolean variable on specified line
    if (!displayInitialized || !oledDisplay) return;
    
    clearLine(line);
    setCursor(0, line * CHAR_HEIGHT_SMALL);
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    printf("%s: %s", label, value ? "TRUE" : "FALSE");
}

void OLED::showString(const char* label, const char* value, uint8_t line) {
    // Display string variable on specified line
    if (!displayInitialized || !oledDisplay) return;
    
    clearLine(line);
    setCursor(0, line * CHAR_HEIGHT_SMALL);
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    printf("%s: %s", label, value ? value : "NULL");
}

// === Public Status Functions ===

void OLED::showHeader(const char* title) {
    // Display title header on first line with larger text
    if (!displayInitialized || !oledDisplay) return;
    
    clearLine(0);
    setCursor(0, 0);
    setTextSize(TEXT_SIZE_MEDIUM);  // Larger text for header
    setTextColor(SSD1306_WHITE);
    print(title);
}

void OLED::showStatus(const char* status) {
    // Display status on last line
    if (!displayInitialized || !oledDisplay) return;
    
    clearLine(7);  // Bottom line for small text
    setCursor(0, 7 * CHAR_HEIGHT_SMALL);
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    print(status);
}

void OLED::showTime(unsigned long milliseconds) {
    // Display uptime/runtime in HH:MM:SS format on top right
    if (!displayInitialized || !oledDisplay) return;
    
    unsigned long seconds = milliseconds / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    setCursor(70, 0);  // Top right area
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    printf("%02lu:%02lu:%02lu", hours, minutes, seconds);
}

// === Public Advanced Functions ===

void OLED::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    // Draw line using Adafruit graphics
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->drawLine(x0, y0, x1, y1, SSD1306_WHITE);
}

void OLED::drawRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Draw rectangle outline
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->drawRect(x, y, w, h, SSD1306_WHITE);
}

void OLED::fillRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Draw filled rectangle
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->fillRect(x, y, w, h, SSD1306_WHITE);
}

void OLED::drawCircle(int16_t x, int16_t y, int16_t r) {
    // Draw circle outline
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->drawCircle(x, y, r, SSD1306_WHITE);
}

void OLED::fillCircle(int16_t x, int16_t y, int16_t r) {
    // Draw filled circle
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->fillCircle(x, y, r, SSD1306_WHITE);
}

// === High-Level Setup Function ===

void OLED::setupOLED() {
    // Complete OLED setup function - call this from your main setup()
    // Handles initialization, error checking, and welcome display
    
    debugPrint("[OLED] Starting OLED display setup using Adafruit library...");
    
    // Attempt to initialize the display with auto-detection
    if (begin()) {
        // Display initialized successfully
        debugPrintf("[OLED] Display ready at address 0x%02X", getAddress());
        
        // Show simple test screen
        clear();
        
        setCursor(0, 0);
        setTextSize(TEXT_SIZE_SMALL);
        print("OLED Test OK");
        
        setCursor(0, 10);
        print("Adafruit Library");
        
        setCursor(0, 20);
        printf("Address: 0x%02X", getAddress());
        
        setCursor(0, 30);
        print("128x64 SSD1306");
        
        setCursor(0, 50);
        print("Ready!");
        
        display();  // Update physical display
        debugPrint("[OLED] Test screen displayed");
        
        // Brief delay to show test message
        delay(3000);
        
        // Show welcome screen
        clear();
        showHeader("Fader Wing");
        
        setCursor(0, 20);
        setTextSize(TEXT_SIZE_SMALL);
        print("OLED: Ready");
        
        setCursor(0, 30);
        printf("Addr: 0x%02X", getAddress());
        
        showStatus("Starting...");
        display();
        
        debugPrint("[OLED] Welcome screen displayed");
        delay(2000);
        
    } else {
        // Display initialization failed
        debugPrint("[OLED] ERROR: Display initialization failed");
        debugPrint("[OLED] Check connections and power");
    }
}

// === Direct Access Function ===

Adafruit_SSD1306* OLED::getDisplay() {
    // Get direct access to Adafruit display object for advanced use
    return oledDisplay;
}

// === Private Helper Functions ===

bool OLED::testAddress(uint8_t address) {
    // Test if display responds at specified I2C address
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}

void OLED::clearLine(uint8_t line, uint8_t textSize) {
    // Clear specific line by drawing black rectangle
    if (!displayInitialized || !oledDisplay) return;
    
    int16_t lineHeight = CHAR_HEIGHT_SMALL * textSize;
    int16_t yPos = line * CHAR_HEIGHT_SMALL;
    
    // Draw black rectangle to clear the line
    oledDisplay->fillRect(0, yPos, SCREEN_WIDTH, lineHeight, SSD1306_BLACK);
}

void OLED::showIPAddress(IPAddress ip) {
    if (!displayInitialized || !oledDisplay) return;

    clearLine(0);  // Clear top line (header)
    setCursor(0, 0);
    setTextSize(TEXT_SIZE_SMALL);
    setTextColor(SSD1306_WHITE);
    printf("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    display();
}

// === Debug Line Buffer ===
#define MAX_DEBUG_LINES 5
static String debugLines[MAX_DEBUG_LINES];
static unsigned long lastDebugDraw = 0;
static const unsigned long debugDrawInterval = 200;  // ms

void OLED::addDebugLine(const char* text) {
    if (!displayInitialized || !oledDisplay || !text) return;

    // Shift lines up
    for (int i = 0; i < MAX_DEBUG_LINES - 1; i++) {
        debugLines[i] = debugLines[i + 1];
    }
    debugLines[MAX_DEBUG_LINES - 1] = String(text);

    // Throttle refresh rate
    unsigned long now = millis();
    if (now - lastDebugDraw < debugDrawInterval) return;
    lastDebugDraw = now;

    // Draw bottom lines
    for (int i = 0; i < MAX_DEBUG_LINES; i++) {
        clearLine(4 + i);
        setCursor(0, (4 + i) * CHAR_HEIGHT_SMALL);
        setTextSize(TEXT_SIZE_SMALL);
        setTextColor(SSD1306_WHITE);
        oledDisplay->print(debugLines[i]);
    }
    oledDisplay->display();
}