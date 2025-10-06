#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include "config.h"
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_MAX1704X.h>
#include "morse_code.h"
#include "training_hear_it_type_it.h"
#include "settings_wifi.h"
#include "settings_cw.h"
#include "training_practice.h"
#include "vail_repeater.h"

// Battery monitor (one of these will be present)
Adafruit_LC709203F lc;
Adafruit_MAX17048 maxlipo;
bool hasLC709203 = false;
bool hasMAX17048 = false;
bool hasBatteryMonitor = false;

// Create display object
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Menu System
enum MenuMode {
  MODE_MAIN_MENU,
  MODE_TRAINING_MENU,
  MODE_HEAR_IT_TYPE_IT,
  MODE_PRACTICE,
  MODE_SETTINGS_MENU,
  MODE_WIFI_SETTINGS,
  MODE_CW_SETTINGS,
  MODE_VAIL_REPEATER,
  MODE_BLUETOOTH
};

MenuMode currentMode = MODE_MAIN_MENU;

// Deep sleep tracking (triple ESC press)
int escPressCount = 0;
unsigned long lastEscPressTime = 0;
#define TRIPLE_ESC_TIMEOUT 2000  // 2 seconds window for 3 presses

String mainMenuOptions[MENU_ITEMS] = {
  "Training",
  "Settings",
  "WiFi",
  "Bluetooth"
};

String mainMenuIcons[MENU_ITEMS] = {
  "T",  // Training
  "S",  // Settings
  "W",  // WiFi
  "B"   // Bluetooth
};

// Training submenu
#define TRAINING_MENU_ITEMS 2
String trainingMenuOptions[TRAINING_MENU_ITEMS] = {
  "Hear It Type It",
  "Practice"
};

String trainingMenuIcons[TRAINING_MENU_ITEMS] = {
  "H",  // Hear It Type It
  "P"   // Practice
};

// Settings submenu
#define SETTINGS_MENU_ITEMS 2
String settingsMenuOptions[SETTINGS_MENU_ITEMS] = {
  "WiFi Setup",
  "CW Settings"
};

String settingsMenuIcons[SETTINGS_MENU_ITEMS] = {
  "W",  // WiFi Setup
  "C"   // CW Settings
};

int currentSelection = 0;
bool menuActive = true;

// Status tracking
bool wifiConnected = false;
int batteryPercent = 100;
bool isCharging = false;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(3000); // Wait for serial monitor to connect
  Serial.println("\n\n=== VAIL SUMMIT STARTING ===");
  Serial.println("Starting setup...");

  // Initialize backlight using PWM (more reliable on ESP32)
  Serial.println("Initializing backlight...");
  ledcAttach(TFT_BL, 5000, 8); // Pin, 5kHz, 8-bit resolution
  ledcWrite(TFT_BL, 255); // Set to full brightness (0-255)
  delay(50);
  Serial.println("Backlight ON");

  // Initialize LCD
  Serial.println("Initializing display...");
  tft.init(240, 320);  // Initialize with hardware dimensions
  tft.setRotation(SCREEN_ROTATION);  // Then rotate to landscape
  tft.fillScreen(COLOR_BACKGROUND);
  Serial.println("Display initialized");

  // Initialize I2C for CardKB
  Serial.println("Initializing I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(100);

  // Initialize Buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  // Initialize Paddle
  pinMode(DIT_PIN, INPUT_PULLUP);
  pinMode(DAH_PIN, INPUT_PULLUP);

  // Initialize USB detection pin
  pinMode(USB_DETECT_PIN, INPUT);

  // Initialize battery monitoring (I2C chip)
  Serial.println("Initializing battery monitor...");

  // Try MAX17048 first (address 0x36) - like Adafruit example
  if (maxlipo.begin()) {
    Serial.print("Found MAX17048 with Chip ID: 0x");
    Serial.println(maxlipo.getChipID(), HEX);
    hasMAX17048 = true;
    hasBatteryMonitor = true;
  }
  // Try LC709203F if MAX not found (address 0x0B)
  else if (lc.begin()) {
    Serial.println("Found LC709203F battery monitor");
    Serial.print("Version: 0x");
    Serial.println(lc.getICversion(), HEX);

    lc.setThermistorB(3950);
    lc.setPackSize(LC709203F_APA_500MAH); // Closest to 350mAh
    lc.setAlarmVoltage(3.8);

    hasLC709203 = true;
    hasBatteryMonitor = true;
  }
  else {
    Serial.println("Could not find MAX17048 or LC709203F battery monitor!");
    Serial.println("Scanning I2C bus...");
    // I2C scan to see what's connected
    for (byte i = 1; i < 127; i++) {
      Wire.beginTransmission(i);
      if (Wire.endTransmission() == 0) {
        Serial.print("Found I2C device at 0x");
        Serial.println(i, HEX);
      }
    }
  }

  // Initialize WiFi and attempt auto-connect
  Serial.println("Initializing WiFi...");
  autoConnectWiFi();
  Serial.println("WiFi initialized");

  // Load CW settings from preferences
  Serial.println("Loading CW settings...");
  loadCWSettings();

  // Ensure backlight is still on
  ledcWrite(TFT_BL, 255);

  // Initial status update
  Serial.println("Updating status...");
  updateStatus();

  // Draw initial menu
  Serial.println("Drawing menu...");
  drawMenu();

  // Final backlight check
  ledcWrite(TFT_BL, 255);
  Serial.println("Setup complete!");

  // Startup beep
  tone(BUZZER_PIN, TONE_STARTUP, BEEP_MEDIUM);
}

void loop() {
  // Update status periodically
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 5000) { // Update every 5 seconds
    updateStatus();
    // Redraw status icons with new data
    drawStatusIcons();
    lastStatusUpdate = millis();
  }

  // Update practice oscillator if in practice mode
  if (currentMode == MODE_PRACTICE) {
    static bool lastDitState = false;
    static bool lastDahState = false;

    updatePracticeOscillator();

    // Update visual feedback if paddle state changed
    bool currentDit = digitalRead(DIT_PIN) == PADDLE_ACTIVE;
    bool currentDah = digitalRead(DAH_PIN) == PADDLE_ACTIVE;

    if (currentDit != lastDitState || currentDah != lastDahState) {
      drawPracticeStats(tft);
      lastDitState = currentDit;
      lastDahState = currentDah;
    }
  }

  // Update Vail repeater if in Vail mode
  if (currentMode == MODE_VAIL_REPEATER) {
    updateVailRepeater(tft);
  }

  // Check for keyboard input
  Wire.requestFrom(CARDKB_ADDR, 1);

  if (Wire.available()) {
    char key = Wire.read();

    if (key != 0) {
      handleKeyPress(key);
    }
  }

  // Reset ESC counter if timeout exceeded
  if (escPressCount > 0 && (millis() - lastEscPressTime > TRIPLE_ESC_TIMEOUT)) {
    escPressCount = 0;
  }

  delay(10);
}

void enterDeepSleep() {
  Serial.println("Entering deep sleep...");

  // Disconnect WiFi if connected
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // Show sleep message
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);

  tft.setCursor(40, 110);
  tft.print("Going to");
  tft.setCursor(50, 140);
  tft.print("Sleep...");

  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF);
  tft.setCursor(30, 180);
  tft.print("Press DIT paddle to wake");

  delay(2000);

  // Turn off display
  tft.fillScreen(ST77XX_BLACK);
  ledcWrite(0, 0);  // Turn off backlight

  // Configure wake on DIT paddle press (active LOW)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)DIT_PIN, LOW);

  // Enter deep sleep
  esp_deep_sleep_start();
  // Device will wake here and restart from setup()
}

void handleKeyPress(char key) {
  bool redraw = false;

  // Handle different modes
  if (currentMode == MODE_HEAR_IT_TYPE_IT) {
    int result = handleHearItTypeItInput(key, tft);
    if (result == 2) {
      // Full redraw requested
      drawHearItTypeItUI(tft);
    } else if (result == 3) {
      // Input box only redraw (faster for typing)
      drawInputBox(tft);
    }
    return;
  }

  // Handle WiFi settings mode
  if (currentMode == MODE_WIFI_SETTINGS) {
    int result = handleWiFiInput(key, tft);
    if (result == -1) {
      // Exit WiFi settings, back to settings menu
      currentMode = MODE_SETTINGS_MENU;
      currentSelection = 0;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      drawMenu();
    } else if (result == 2) {
      // Full redraw requested
      drawWiFiUI(tft);
    }
    return;
  }

  // Handle CW settings mode
  if (currentMode == MODE_CW_SETTINGS) {
    int result = handleCWSettingsInput(key, tft);
    if (result == -1) {
      // Exit CW settings, back to settings menu
      currentMode = MODE_SETTINGS_MENU;
      currentSelection = 0;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      drawMenu();
    }
    return;
  }

  // Handle Practice mode
  if (currentMode == MODE_PRACTICE) {
    int result = handlePracticeInput(key, tft);
    if (result == -1) {
      // Exit practice mode, back to training menu
      currentMode = MODE_TRAINING_MENU;
      currentSelection = 0;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      drawMenu();
    }
    return;
  }

  // Handle Vail repeater mode
  if (currentMode == MODE_VAIL_REPEATER) {
    int result = handleVailInput(key, tft);
    if (result == -1) {
      // Exit Vail mode, back to main menu
      currentMode = MODE_MAIN_MENU;
      currentSelection = 0;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      drawMenu();
    }
    return;
  }

  // Menu navigation (for MAIN_MENU, TRAINING_MENU, and SETTINGS_MENU)
  if (currentMode == MODE_MAIN_MENU || currentMode == MODE_TRAINING_MENU || currentMode == MODE_SETTINGS_MENU) {
    int maxItems = MENU_ITEMS;
    if (currentMode == MODE_TRAINING_MENU) maxItems = TRAINING_MENU_ITEMS;
    if (currentMode == MODE_SETTINGS_MENU) maxItems = SETTINGS_MENU_ITEMS;

    // Arrow key navigation
    if (key == KEY_UP) {
      if (currentSelection > 0) {
        currentSelection--;
        redraw = true;
        tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      }
    }
    else if (key == KEY_DOWN) {
      if (currentSelection < maxItems - 1) {
        currentSelection++;
        redraw = true;
        tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      }
    }
    else if (key == KEY_ENTER || key == KEY_ENTER_ALT) {
      selectMenuItem();
    }
    else if (key == KEY_ESC) {
      if (currentMode == MODE_TRAINING_MENU || currentMode == MODE_SETTINGS_MENU) {
        // Back to main menu
        currentMode = MODE_MAIN_MENU;
        currentSelection = 0;
        tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
        drawMenu();
        return;
      } else if (currentMode == MODE_MAIN_MENU) {
        // In main menu - count ESC presses for sleep (triple tap)
        escPressCount++;
        lastEscPressTime = millis();

        if (escPressCount >= 3) {
          // Triple ESC pressed - enter sleep
          tone(BUZZER_PIN, TONE_STARTUP, 200);
          delay(200);
          enterDeepSleep();
        } else {
          // Beep for each press to give feedback
          tone(BUZZER_PIN, TONE_MENU_NAV, 50);
        }
      }
    }

    if (redraw) {
      if (currentMode == MODE_MAIN_MENU) {
        drawMenuItems(mainMenuOptions, mainMenuIcons, MENU_ITEMS);
      } else if (currentMode == MODE_TRAINING_MENU) {
        drawMenuItems(trainingMenuOptions, trainingMenuIcons, TRAINING_MENU_ITEMS);
      } else if (currentMode == MODE_SETTINGS_MENU) {
        drawMenuItems(settingsMenuOptions, settingsMenuIcons, SETTINGS_MENU_ITEMS);
      }
    }
  }
}

void drawHeader() {
  // Draw modern header bar
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, 0x1082); // Dark blue header

  // Draw title based on current mode
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  String title = "VAIL SUMMIT";

  if (currentMode == MODE_TRAINING_MENU) {
    title = "TRAINING";
  } else if (currentMode == MODE_HEAR_IT_TYPE_IT) {
    title = "TRAINING";
  } else if (currentMode == MODE_PRACTICE) {
    title = "PRACTICE";
  } else if (currentMode == MODE_SETTINGS_MENU) {
    title = "SETTINGS";
  } else if (currentMode == MODE_WIFI_SETTINGS) {
    title = "WIFI SETUP";
  } else if (currentMode == MODE_CW_SETTINGS) {
    title = "CW SETTINGS";
  } else if (currentMode == MODE_VAIL_REPEATER) {
    title = "VAIL CHAT";  // Also updates header
  }

  tft.setCursor(10, 27); // Left-justified
  tft.print(title);
  tft.setFont(); // Reset to default font

  // Draw status icons
  drawStatusIcons();

  // Draw subtle shadow line under header
  tft.drawLine(0, 40, SCREEN_WIDTH, 40, 0x2104);
  tft.drawLine(0, 41, SCREEN_WIDTH, 41, 0x0861);
}

void drawMenu() {
  tft.fillScreen(COLOR_BACKGROUND);

  drawHeader();

  // Draw footer (only for menu modes)
  if (currentMode == MODE_MAIN_MENU || currentMode == MODE_TRAINING_MENU || currentMode == MODE_SETTINGS_MENU) {
    drawFooter();
  }

  // Draw menu items or mode-specific UI
  if (currentMode == MODE_MAIN_MENU) {
    drawMenuItems(mainMenuOptions, mainMenuIcons, MENU_ITEMS);
  } else if (currentMode == MODE_TRAINING_MENU) {
    drawMenuItems(trainingMenuOptions, trainingMenuIcons, TRAINING_MENU_ITEMS);
  } else if (currentMode == MODE_SETTINGS_MENU) {
    drawMenuItems(settingsMenuOptions, settingsMenuIcons, SETTINGS_MENU_ITEMS);
  } else if (currentMode == MODE_HEAR_IT_TYPE_IT) {
    drawHearItTypeItUI(tft);
  } else if (currentMode == MODE_PRACTICE) {
    drawPracticeUI(tft);
  } else if (currentMode == MODE_WIFI_SETTINGS) {
    drawWiFiUI(tft);
  } else if (currentMode == MODE_CW_SETTINGS) {
    drawCWSettingsUI(tft);
  } else if (currentMode == MODE_VAIL_REPEATER) {
    drawVailUI(tft);
  }
}

void drawMenuItems(String options[], String icons[], int numItems) {
  // Clear only the menu area (between header and footer)
  tft.fillRect(0, 42, SCREEN_WIDTH, SCREEN_HEIGHT - 42 - 20, COLOR_BACKGROUND);

  // Draw menu items with carousel/stack design
  // Main selected card (larger, using more screen space)
  int mainCardWidth = 300;
  int mainCardHeight = 60;
  int mainCardX = (SCREEN_WIDTH - mainCardWidth) / 2;
  int mainCardY = 85; // Moved down to avoid clipping with header

  // Draw the selected card (large and prominent)
  tft.fillRoundRect(mainCardX, mainCardY, mainCardWidth, mainCardHeight, 8, 0x249F); // Blue accent
  tft.drawRoundRect(mainCardX, mainCardY, mainCardWidth, mainCardHeight, 8, 0x34BF); // Lighter outline

  // Draw icon circle for selected
  tft.fillCircle(mainCardX + 30, mainCardY + 30, 20, 0x34BF);
  tft.drawCircle(mainCardX + 30, mainCardY + 30, 20, ST77XX_WHITE); // White outline
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(mainCardX + 23, mainCardY + 20); // Letter centered in circle
  tft.print(icons[currentSelection]);

  // Draw menu text for selected (slightly larger)
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(mainCardX + 65, mainCardY + 22);
  tft.print(options[currentSelection]);

  // Draw selection arrow
  tft.fillTriangle(mainCardX + mainCardWidth - 20, mainCardY + 25,
                   mainCardX + mainCardWidth - 20, mainCardY + 35,
                   mainCardX + mainCardWidth - 10, mainCardY + 30, ST77XX_WHITE);

  // Draw stacked cards underneath (previous items)
  int stackCardWidth = 270;
  int stackCardHeight = 24;
  int stackCardX = (SCREEN_WIDTH - stackCardWidth) / 2;
  int stackOffset = 10;

  // Draw card below (next item in list)
  if (currentSelection < numItems - 1) {
    int stackY1 = mainCardY + mainCardHeight + stackOffset;
    tft.fillRoundRect(stackCardX, stackY1, stackCardWidth, stackCardHeight, 6, 0x2104);

    // Draw small circle for icon
    tft.drawCircle(stackCardX + 12, stackY1 + 12, 8, 0x4208);
    tft.setTextSize(1);
    tft.setTextColor(0x7BEF);
    tft.setCursor(stackCardX + 10, stackY1 + 9); // Letter centered in circle
    tft.print(icons[currentSelection + 1]);

    // Draw text
    tft.setCursor(stackCardX + 28, stackY1 + 8);
    tft.print(options[currentSelection + 1]);
  }

  // Draw card further below (next+1 item)
  if (currentSelection < numItems - 2) {
    int stackY2 = mainCardY + mainCardHeight + stackOffset + stackCardHeight + 6;
    int stackCardWidth2 = 250;
    int stackCardX2 = (SCREEN_WIDTH - stackCardWidth2) / 2;
    tft.fillRoundRect(stackCardX2, stackY2, stackCardWidth2, 18, 4, 0x1082);

    // Draw small circle for icon
    tft.drawCircle(stackCardX2 + 10, stackY2 + 9, 6, 0x3186);
    tft.setTextSize(1);
    tft.setTextColor(0x5AEB);
    tft.setCursor(stackCardX2 + 8, stackY2 + 6); // Letter centered in circle
    tft.print(icons[currentSelection + 2]);

    // Draw text
    tft.setCursor(stackCardX2 + 22, stackY2 + 5);
    tft.print(options[currentSelection + 2]);
  }

  // Draw card above (previous item in list)
  if (currentSelection > 0) {
    int stackY0 = mainCardY - stackCardHeight - stackOffset;
    tft.fillRoundRect(stackCardX, stackY0, stackCardWidth, stackCardHeight, 6, 0x2104);

    // Draw small circle for icon
    tft.drawCircle(stackCardX + 12, stackY0 + 12, 8, 0x4208);
    tft.setTextSize(1);
    tft.setTextColor(0x7BEF);
    tft.setCursor(stackCardX + 10, stackY0 + 9); // Letter centered in circle
    tft.print(icons[currentSelection - 1]);

    // Draw text
    tft.setCursor(stackCardX + 28, stackY0 + 8);
    tft.print(options[currentSelection - 1]);
  }
}

void drawStatusIcons() {
  int iconX = SCREEN_WIDTH - 10; // Start from right edge
  int iconY = 13; // Vertically centered in 40px header

  // Draw battery icon (now larger - 26px wide including nub)
  iconX -= 30;
  drawBatteryIcon(iconX, iconY);

  // Draw WiFi icon
  iconX -= 25;
  drawWiFiIcon(iconX, iconY);
}

void drawBatteryIcon(int x, int y) {
  // Battery outline (24x14 pixels - larger for better visibility)
  tft.drawRect(x, y, 24, 14, ST77XX_WHITE);
  tft.fillRect(x + 24, y + 4, 2, 6, ST77XX_WHITE); // Battery nub

  // Determine battery color based on percentage
  uint16_t fillColor;
  if (batteryPercent > 60) {
    fillColor = ST77XX_GREEN;
  } else if (batteryPercent > 20) {
    fillColor = ST77XX_YELLOW;
  } else {
    fillColor = ST77XX_RED;
  }

  // Fill battery based on percentage
  int fillWidth = (batteryPercent * 20) / 100; // 20 pixels max fill

  if (DEBUG_ENABLED) {
    Serial.print("Drawing battery: ");
    Serial.print(batteryPercent);
    Serial.print("% fillWidth=");
    Serial.print(fillWidth);
    Serial.print(" charging=");
    Serial.println(isCharging ? "YES" : "NO");
  }

  if (fillWidth > 0) {
    tft.fillRect(x + 2, y + 2, fillWidth, 10, fillColor);
  }

  // Draw charging indicator (white lightning bolt with black outline for contrast)
  if (isCharging) {
    // Draw black outline first for better visibility
    tft.fillTriangle(x + 13, y + 3, x + 10, y + 8, x + 16, y + 8, ST77XX_BLACK);
    tft.fillTriangle(x + 10, y + 8, x + 13, y + 13, x + 9, y + 8, ST77XX_BLACK);
    // Draw white lightning bolt on top
    tft.fillTriangle(x + 14, y + 4, x + 11, y + 8, x + 15, y + 8, ST77XX_WHITE);
    tft.fillTriangle(x + 11, y + 8, x + 14, y + 12, x + 10, y + 8, ST77XX_WHITE);
  }
}

void drawWiFiIcon(int x, int y) {
  // WiFi icon (signal bars)
  uint16_t wifiColor = wifiConnected ? ST77XX_GREEN : ST77XX_RED;

  // Draw signal strength bars (4 bars, increasing height)
  tft.fillRect(x, y + 8, 2, 2, wifiColor);      // Shortest bar
  tft.fillRect(x + 4, y + 6, 2, 4, wifiColor);  // Medium-short bar
  tft.fillRect(x + 8, y + 4, 2, 6, wifiColor);  // Medium-tall bar
  tft.fillRect(x + 12, y + 2, 2, 8, wifiColor); // Tallest bar
}

void updateStatus() {
  // Update WiFi status
  wifiConnected = WiFi.status() == WL_CONNECTED;

  // Read battery voltage and percentage from I2C battery monitor
  float voltage = 3.7; // Default
  batteryPercent = 50;

  if (hasLC709203) {
    voltage = lc.cellVoltage();
    batteryPercent = (int)lc.cellPercent();
  }
  else if (hasMAX17048) {
    voltage = maxlipo.cellVoltage();
    batteryPercent = (int)maxlipo.cellPercent();
  }
  else {
    // No battery monitor - show placeholder values
    voltage = 3.7;
    batteryPercent = 50;
  }

  // Validate readings
  if (voltage < 2.5 || voltage > 5.0) {
    voltage = 3.7;
  }

  // Constrain to 0-100%
  if (batteryPercent > 100) batteryPercent = 100;
  if (batteryPercent < 0) batteryPercent = 0;

  // Detect USB power using the USB detection pin
  // The Feather has a USB pin that provides voltage when USB is connected
  int usbReading = analogRead(USB_DETECT_PIN);

  // If we detect voltage on USB pin, USB power is connected
  // This is more reliable than trying to infer from battery voltage
  if (usbReading > 500) {  // Threshold for USB presence (adjust if needed)
    isCharging = true;
    Serial.print("USB detected (ADC: ");
    Serial.print(usbReading);
    Serial.println(")");
  } else {
    isCharging = false;
  }

  // Debug output
  if (DEBUG_ENABLED) {
    Serial.print("Battery: ");
    Serial.print(voltage);
    Serial.print("V (");
    Serial.print(batteryPercent);
    Serial.print("%) ");
    Serial.print(isCharging ? "CHARGING" : "");
    Serial.print(" | WiFi: ");
    Serial.println(wifiConnected ? "Connected" : "Disconnected");
  }
}

void drawFooter() {
  // Draw modern footer with instructions (single line centered in yellow)
  int footerY = SCREEN_HEIGHT - 12;
  tft.setTextSize(1);
  tft.setTextColor(COLOR_WARNING); // Yellow

  String helpText;
  if (currentMode == MODE_MAIN_MENU) {
    helpText = "\x18\x19 Navigate  ENTER Select  ESC x3 Sleep";
  } else {
    helpText = "\x18\x19 Navigate  ENTER Select  ESC Back";
  }

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(helpText, 0, 0, &x1, &y1, &w, &h);
  int centerX = (SCREEN_WIDTH - w) / 2;
  tft.setCursor(centerX, footerY);
  tft.print(helpText);
}

void selectMenuItem() {
  // Play confirmation beep
  tone(BUZZER_PIN, TONE_SELECT, BEEP_MEDIUM);

  String selectedItem;

  if (currentMode == MODE_MAIN_MENU) {
    selectedItem = mainMenuOptions[currentSelection];

    // Handle main menu selections
    if (currentSelection == 0) {
      // Training
      currentMode = MODE_TRAINING_MENU;
      currentSelection = 0;
      drawMenu();

    } else if (currentSelection == 1) {
      // Settings
      currentMode = MODE_SETTINGS_MENU;
      currentSelection = 0;
      drawMenu();

    } else if (currentSelection == 2) {
      // WiFi (Vail Repeater)
      if (WiFi.status() != WL_CONNECTED) {
        // Not connected to WiFi
        tft.fillScreen(COLOR_BACKGROUND);
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(30, 100);
        tft.print("Connect WiFi");
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(20, 130);
        tft.print("Settings > WiFi Setup");
        delay(2000);
        drawMenu();
      } else {
        // Connected to WiFi, start Vail repeater
        currentMode = MODE_VAIL_REPEATER;
        startVailRepeater(tft);
        connectToVail(vailChannel);  // Use default channel
      }

    } else if (currentSelection == 3) {
      // Bluetooth
      tft.fillScreen(COLOR_BACKGROUND);
      tft.setTextSize(2);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(50, 100);
      tft.print("Bluetooth coming soon");
      delay(1500);
      drawMenu();
    }

  } else if (currentMode == MODE_TRAINING_MENU) {
    selectedItem = trainingMenuOptions[currentSelection];

    // Handle training menu selections
    if (currentSelection == 0) {
      // Hear It Type It
      currentMode = MODE_HEAR_IT_TYPE_IT;
      randomSeed(analogRead(0)); // Seed random number generator
      startNewCallsign();
      drawMenu();
      delay(1000); // Brief pause before starting
      playCurrentCallsign();
      drawHearItTypeItUI(tft);
    } else if (currentSelection == 1) {
      // Practice
      currentMode = MODE_PRACTICE;
      startPracticeMode(tft);
    }
  } else if (currentMode == MODE_SETTINGS_MENU) {
    selectedItem = settingsMenuOptions[currentSelection];

    // Handle settings menu selections
    if (currentSelection == 0) {
      // WiFi Setup
      currentMode = MODE_WIFI_SETTINGS;
      startWiFiSettings(tft);
    } else if (currentSelection == 1) {
      // CW Settings
      currentMode = MODE_CW_SETTINGS;
      startCWSettings(tft);
    }
  }
}
