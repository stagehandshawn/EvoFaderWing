// === OLED.h ===
// Simple OLED wrapper using Adafruit SSD1306 library
// Designed for Teensy 4.1 fader wing project
// Provides easy variable display functions for your 128x64 SSD1306 display

#ifndef OLED_H
#define OLED_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IPAddress.h>

// === OLED Configuration Constants ===
#define SCREEN_WIDTH 128        // OLED display width in pixels
#define SCREEN_HEIGHT 64        // OLED display height in pixels
#define OLED_RESET -1          // Reset pin (not used for I2C displays)
#define OLED_ADDR_PRIMARY 0x3C  // Primary I2C address (most common)
#define OLED_ADDR_SECONDARY 0x3D // Secondary I2C address (alternative)

// === Display Layout Constants ===
#define TEXT_SIZE_SMALL 1       // Small text (6x8 pixels per character)
#define TEXT_SIZE_MEDIUM 2      // Medium text (12x16 pixels per character)
#define TEXT_SIZE_LARGE 3       // Large text (18x24 pixels per character)
#define CHAR_HEIGHT_SMALL 8     // Height of small characters
#define CHAR_HEIGHT_MEDIUM 16   // Height of medium characters
#define MAX_LINES_SMALL 8       // Maximum lines with small text
#define MAX_LINES_MEDIUM 4      // Maximum lines with medium text

// === OLED Wrapper Class ===
class OLED {
private:
    Adafruit_SSD1306* oledDisplay;     // Pointer to Adafruit display object
    uint8_t i2cAddress;                // Current I2C address being used
    bool displayInitialized;           // Flag to track initialization status
    
    // === Private Helper Functions ===
    bool testAddress(uint8_t address);     // Test if display responds at address
    bool initializeDisplay();              // Initialize display with SSD1306 sequence
    void clearLine(uint8_t line, uint8_t textSize = TEXT_SIZE_SMALL); // Clear specific line
    
public:
    // === Constructor and Destructor ===
    OLED();
    ~OLED();
    
    // === Public Initialization Functions ===
    bool begin();                          // Auto-detect address and initialize display
    bool begin(uint8_t address);           // Initialize with specific I2C address
    bool isInitialized();                  // Check if display is ready
    uint8_t getAddress();                  // Get current I2C address
    
    // === Public Display Control Functions ===
    void clear();                          // Clear display
    void display();                        // Update physical display
    void setBrightness(uint8_t brightness); // Set display brightness (0-255)
    void setInverted(bool inverted);       // Set normal/inverted display mode
    void powerOff();                       // Turn display off (sleep mode)
    void powerOn();                        // Turn display on (wake from sleep)
    
    // === High-Level Setup Functions ===
    void setupOLED();                      // Complete OLED setup with initialization and welcome display
    
    // === Public Text Functions ===
    void setCursor(uint8_t x, uint8_t y);  // Set cursor position in pixels
    void setTextSize(uint8_t size);        // Set text size (1, 2, or 3)
    void setTextColor(uint16_t color);     // Set text color (WHITE, BLACK, INVERSE)
    void print(const char* text);          // Print text string at current cursor
    void println(const char* text);        // Print text string with newline
    void printf(const char* format, ...);  // Print formatted text
    
    // === Public Variable Display Functions (Small Text) ===
    void showInt(const char* label, int value, uint8_t line);        // Display integer variable
    void showFloat(const char* label, float value, uint8_t line);    // Display float variable
    void showBool(const char* label, bool value, uint8_t line);      // Display boolean variable
    void showString(const char* label, const char* value, uint8_t line); // Display string variable
    
    // === Public Status Functions ===
    void showHeader(const char* title);    // Display title header on first line
    void showStatus(const char* status);   // Display status on last line
    void showTime(unsigned long millis);   // Display uptime/runtime

//    void OLED::showIPAddress(IPAddress ip, uint16_t recvPort, IPAddress sendIP, uint16_t sendPort);
    void showIPAddress(IPAddress ip, uint16_t recvPort, IPAddress sendIP, uint16_t sendPort);

    // === Public Advanced Functions ===
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1); // Draw line
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h);     // Draw rectangle
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h);     // Draw filled rectangle
    void drawCircle(int16_t x, int16_t y, int16_t r);              // Draw circle
    void fillCircle(int16_t x, int16_t y, int16_t r);              // Draw filled circle
    
    // === Direct Access to Adafruit Display Object ===
    Adafruit_SSD1306* getDisplay();       // Get direct access to Adafruit display object for advanced use


    // === Debug Display Function ===
    void addDebugLine(const char* text);
    void clearDebugLines();
};

// === External Debug Functions (assumed to exist in your project) ===
// These should be defined in your main project's Utils.cpp or similar
extern void debugPrint(const char* message);
extern void debugPrintf(const char* format, ...);

extern OLED display;  // <- declaration

#endif // OLED_H