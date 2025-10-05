/*
 * Morse Code Trainer - Hardware Configuration
 * ESP32-S3 Feather Pin Definitions and Settings
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// LCD Display (ST7789V) - SPI Interface
// ============================================
#define TFT_CS      10    // Chip Select
#define TFT_DC      11    // Data/Command
#define TFT_RST     12    // Reset
#define TFT_BL      13    // Backlight
#define TFT_MOSI    35    // SPI Data (hardware SPI)
#define TFT_SCK     36    // SPI Clock (hardware SPI)

// Display Settings
#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   240
#define SCREEN_ROTATION 1     // 0=Portrait, 1=Landscape, 2=Portrait flipped, 3=Landscape flipped

// ============================================
// CardKB Keyboard - I2C Interface
// ============================================
#define CARDKB_ADDR 0x5F  // I2C Address
#define I2C_SDA     3     // I2C Data (STEMMA QT)
#define I2C_SCL     4     // I2C Clock (STEMMA QT)

// CardKB Special Key Codes
#define KEY_UP      0xB5  // Up arrow
#define KEY_DOWN    0xB6  // Down arrow
#define KEY_LEFT    0xB4  // Left arrow
#define KEY_RIGHT   0xB7  // Right arrow
#define KEY_ENTER   0x0D  // Enter/Return
#define KEY_ENTER_ALT 0x0A // Alternate Enter
#define KEY_BACKSPACE 0x08 // Backspace (Fn+X)
#define KEY_ESC     0x1B  // ESC (Fn+Z)
#define KEY_TAB     0x09  // Tab (Fn+Space)

// ============================================
// Buzzer - PWM Output
// ============================================
#define BUZZER_PIN  5     // PWM output for buzzer

// Audio Settings
#define TONE_SIDETONE   700   // Hz - Morse code audio tone
#define TONE_MENU_NAV   800   // Hz - Menu navigation beep
#define TONE_SELECT     1200  // Hz - Selection confirmation
#define TONE_ERROR      400   // Hz - Error/invalid beep
#define TONE_STARTUP    1000  // Hz - Startup beep

#define BEEP_SHORT      30    // ms - Short beep duration
#define BEEP_MEDIUM     100   // ms - Medium beep duration
#define BEEP_LONG       200   // ms - Long beep duration

// ============================================
// Iambic Paddle Key - Digital Inputs
// ============================================
#define DIT_PIN     6     // Dit paddle (tip on 3.5mm jack)
#define DAH_PIN     9     // Dah paddle (ring on 3.5mm jack)
                          // Sleeve = GND

// Paddle Settings
#define PADDLE_ACTIVE   LOW   // Paddles are active LOW (pullup enabled)

// ============================================
// Battery Monitoring
// ============================================
// ESP32-S3 Feather V2: Uses MAX17048 I2C fuel gauge at 0x36
// USB detection on dedicated USB pin
#define USB_DETECT_PIN  A3    // USB voltage detection pin (BAT pin is A13)

// Battery voltage thresholds (for LiPo)
#define VBAT_FULL   4.2   // Fully charged voltage
#define VBAT_EMPTY  3.3   // Empty voltage (cutoff)

// ============================================
// Morse Code Timing Settings
// ============================================
#define DEFAULT_WPM     20    // Words per minute
#define WPM_MIN         5     // Minimum WPM
#define WPM_MAX         40    // Maximum WPM

// Calculate dit duration in milliseconds from WPM
// Standard: PARIS method (50 dit units per word)
#define DIT_DURATION(wpm) (1200 / wpm)

// ============================================
// Serial Debug
// ============================================
#define SERIAL_BAUD 115200
#define DEBUG_ENABLED true

// ============================================
// UI Color Scheme
// ============================================
#define COLOR_BACKGROUND    ST77XX_BLACK
#define COLOR_TITLE         ST77XX_CYAN
#define COLOR_TEXT          ST77XX_WHITE
#define COLOR_HIGHLIGHT_BG  ST77XX_BLUE
#define COLOR_HIGHLIGHT_FG  ST77XX_WHITE
#define COLOR_SUCCESS       ST77XX_GREEN
#define COLOR_ERROR         ST77XX_RED
#define COLOR_WARNING       ST77XX_YELLOW
#define COLOR_SEPARATOR     ST77XX_WHITE

// ============================================
// Menu Configuration
// ============================================
#define MENU_ITEMS      4
#define MENU_START_Y    55
#define MENU_ITEM_HEIGHT 35
#define MENU_TEXT_SIZE  2

#endif // CONFIG_H
