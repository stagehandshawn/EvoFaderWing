// === OLED.cpp ===
// Simple OLED wrapper using Adafruit SSD1306 library
// Designed for Teensy 4.1 fader wing project
// Provides helper functions for a 128x64 SSD1306 display

#include "OLED.h"
#include <stdarg.h>
#include <stdio.h>
#include <IPAddress.h>
#include "Config.h"
#include "Utils.h"

static bool ipScreenDirty = true; // track when IP screen needs redraw

// === Constructor and Destructor ===

OLED::OLED() {
    oledDisplay = nullptr;
    i2cAddress = 0;
    displayInitialized = false;
}

OLED::~OLED() {
    if (oledDisplay) {
        //delete oledDisplay;
        //oledDisplay = nullptr;
        oledDisplay.reset();   // New method
    }
}

// === Public Initialization Functions ===

bool OLED::begin() {
    // Try to auto-detect the display address by testing common addresses
    OLED_DEBUG_PRINT("Auto-detect...");
    
    // Test the primary address first because it is the most common variant
    if (testAddress(OLED_ADDR_PRIMARY)) {
        i2cAddress = OLED_ADDR_PRIMARY;
        OLED_DEBUG_PRINTF("Found 0x%02X", i2cAddress);
    }
    // Test secondary address if primary failed
    else if (testAddress(OLED_ADDR_SECONDARY)) {
        i2cAddress = OLED_ADDR_SECONDARY;
        OLED_DEBUG_PRINTF("Found 0x%02X", i2cAddress);
    }
    else {
        OLED_ERROR_PRINT("No display");
        return false;
    }
    
    // Create Adafruit display object with detected address

    //oledDisplay = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
    oledDisplay = std::make_unique<Adafruit_SSD1306>(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);   // New method
    
    // Initialize the display using Adafruit library
    if (!oledDisplay->begin(SSD1306_SWITCHCAPVCC, i2cAddress)) {
        OLED_ERROR_PRINT("alloc failed");
        //delete oledDisplay;
        //oledDisplay = nullptr;
        oledDisplay.reset();  // New Method
        return false;
    }
    
    // Configure display settings
    oledDisplay->clearDisplay();
    oledDisplay->setTextSize(TEXT_SIZE_SMALL);      // Small text by default
    oledDisplay->setTextColor(SSD1306_WHITE);       // White text
    oledDisplay->cp437(true);                       // Use full 256 character 'Code Page 437' font
    
    displayInitialized = true;
    OLED_DEBUG_PRINT("Init ok");
    return true;
}

bool OLED::begin(uint8_t address) {
    // Initialize with user-specified address
    OLED_DEBUG_PRINTF("Init at 0x%02X", address);
    
    if (testAddress(address)) {
        i2cAddress = address;
        
        // Create Adafruit display object
        //oledDisplay = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
        oledDisplay = std::make_unique<Adafruit_SSD1306>(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);   // New method
        
        // Initialize the display using Adafruit library
        if (!oledDisplay->begin(SSD1306_SWITCHCAPVCC, i2cAddress)) {
            OLED_ERROR_PRINT("alloc failed");
            //delete oledDisplay;
            //oledDisplay = nullptr;
            oledDisplay.reset();  //New method
            return false;
        }
        
        // Configure display settings
        oledDisplay->clearDisplay();
        oledDisplay->setTextSize(TEXT_SIZE_SMALL);
        oledDisplay->setTextColor(SSD1306_WHITE);
        oledDisplay->cp437(true);
        
        displayInitialized = true;
        OLED_DEBUG_PRINT("Init ok");
        return true;
    } else {
        OLED_ERROR_PRINTF("No dsp at 0x%02X", address);
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
    ipScreenDirty = true;
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
    OLED_DEBUG_PRINTF("Brightness set to %d", brightness);
}

void OLED::setInverted(bool inverted) {
    // Set normal or inverted display mode
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->invertDisplay(inverted);
    OLED_DEBUG_PRINTF("Display mode: %s", inverted ? "INVERTED" : "NORMAL");
}

void OLED::powerOff() {
    // Turn display off (sleep mode)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->ssd1306_command(SSD1306_DISPLAYOFF);
    OLED_DEBUG_PRINT("Display powered off");
}

void OLED::powerOn() {
    // Turn display on (wake from sleep)
    if (!displayInitialized || !oledDisplay) return;
    oledDisplay->ssd1306_command(SSD1306_DISPLAYON);
    OLED_DEBUG_PRINT("Display powered on");
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
    
    ipScreenDirty = true;
    clearLine(0);
    setCursor(0, 0);
    setTextSize(TEXT_SIZE_SMALL);  // Larger text for header
    setTextColor(SSD1306_WHITE);
    print(title);
    display();
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
    // Complete OLED setup helper for the main startup path
    // Handles initialization, error checking, and welcome display
    
    OLED_DEBUG_PRINT("Starting OLED");
    
    // Attempt to initialize the display with auto-detection
    if (begin()) {
        // Display initialized successfully
        // debugPrintf("[OLED] Display at 0x%02X", getAddress());
        
        // // Show simple test screen
        // clear();
        
        // setCursor(0, 0);
        // setTextSize(TEXT_SIZE_SMALL);
        // print("OLED Test OK");
        
        // setCursor(0, 10);
        // print("Adafruit Library");
        
        // setCursor(0, 20);
        // printf("Address: 0x%02X", getAddress());
        
        // setCursor(0, 30);
        // print("128x64 SSD1306");
        
        // setCursor(0, 50);
        // print("Ready!");
        
        // display();  // Update physical display
        // debugPrint("[OLED] Test screen");
        
        // // Brief delay to show test message
        // delay(1000);
        
        // Show welcome screen
        clear();
        showHeader("EvoFaderWing") ;
        setCursor(0, 20);
        setTextSize(TEXT_SIZE_SMALL);
        printf("Version: %s", SW_VERSION);

        display();


        // setCursor(0, 20);
        // setTextSize(TEXT_SIZE_SMALL);
        // print("OLED: Ready");
        
        // setCursor(0, 30);
        // printf("Addr: 0x%02X", getAddress());
        
        // showStatus("Starting...");
        // display();
        
        // debugPrint("[OLED] init OK");
        delay(2000);
        
    } else {
        // Display initialization failed
        OLED_ERROR_PRINT("Init failed");
        OLED_ERROR_PRINT("Check wiring");
    }
}

// === Direct Access Function ===

Adafruit_SSD1306* OLED::getDisplay() {
    // Get direct access to Adafruit display object for advanced use
    //return oledDisplay;
    return oledDisplay.get();
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

void OLED::clearDebugLines() {
    // Debug output to OLED is disabled.
}

void OLED::showIPAddress(IPAddress ip, uint16_t oscPort, IPAddress sendIP) {
    showNetworkStatus(OLEDNetworkState::ConnectedStatic, ip, oscPort, sendIP);
}

void OLED::showNetworkStatus(OLEDNetworkState state, IPAddress ip, uint16_t oscPort, IPAddress sendIP) {
    if (!displayInitialized || !oledDisplay) return;

    static OLEDNetworkState lastState = OLEDNetworkState::LinkDown;
    static IPAddress lastIP(0, 0, 0, 0);
    static IPAddress lastSendIP(0, 0, 0, 0);
    static uint16_t lastOscPort = 0;
    static bool hasLast = false;

    if (!ipScreenDirty && hasLast &&
        state == lastState &&
        ip == lastIP &&
        oscPort == lastOscPort &&
        sendIP == lastSendIP) {
        return;
    }

    clear();

    setCursor(0, 0);
    setTextSize(TEXT_SIZE_SMALL);
    print("EvoFaderWing");

    setCursor(0, CHAR_HEIGHT_SMALL * 2);
    switch (state) {
        case OLEDNetworkState::LinkDown:
            print("Link Down");
            break;
        case OLEDNetworkState::ConnectingLink:
            print("Connecting Link");
            break;
        case OLEDNetworkState::ConnectingDhcp:
            print("Connecting DHCP");
            break;
        case OLEDNetworkState::ConnectingStatic:
            print("Connecting Static");
            break;
        case OLEDNetworkState::ConnectedDhcp:
            print("Connected DHCP");
            break;
        case OLEDNetworkState::ConnectedStatic:
            print("Connected Static");
            break;
        case OLEDNetworkState::DhcpStaticFallback:
            print("DHCP->Static");
            break;
    }

    setCursor(0, CHAR_HEIGHT_SMALL * 3);
    printf("%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], oscPort);

    setCursor(0, CHAR_HEIGHT_SMALL * 5);
    print("grandMA3:");

    setCursor(0, CHAR_HEIGHT_SMALL * 6);
    printf("%d.%d.%d.%d:%d", sendIP[0], sendIP[1], sendIP[2], sendIP[3], oscPort);

    display();

    lastState = state;
    lastIP = ip;
    lastSendIP = sendIP;
    lastOscPort = oscPort;
    hasLast = true;
    ipScreenDirty = false;
}

void OLED::markDirty() {
    ipScreenDirty = true;
}

void OLED::addDebugLine(const char* text) {
    // Debug output to OLED is disabled.
}
