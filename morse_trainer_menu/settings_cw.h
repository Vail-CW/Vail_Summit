/*
 * CW Settings Module
 * Handles morse code speed, tone, and key type settings
 */

#ifndef SETTINGS_CW_H
#define SETTINGS_CW_H

#include <Preferences.h>
#include "config.h"

// Key types
enum KeyType {
  KEY_STRAIGHT,
  KEY_IAMBIC_A,
  KEY_IAMBIC_B
};

// CW settings state
enum CWSettingsState {
  CW_SETTING_SPEED,
  CW_SETTING_TONE,
  CW_SETTING_KEY_TYPE
};

// CW settings globals
CWSettingsState cwSettingState = CW_SETTING_SPEED;
int cwSpeed = DEFAULT_WPM;         // WPM
int cwTone = TONE_SIDETONE;        // Hz
KeyType cwKeyType = KEY_IAMBIC_B;  // Default to Iambic B
Preferences cwPrefs;

// Setting selection
int cwSettingSelection = 0;
#define CW_SETTINGS_COUNT 3

// Forward declarations
void startCWSettings(Adafruit_ST7789 &display);
void drawCWSettingsUI(Adafruit_ST7789 &display);
int handleCWSettingsInput(char key, Adafruit_ST7789 &display);
void saveCWSettings();
void loadCWSettings();

// Load CW settings from flash
void loadCWSettings() {
  cwPrefs.begin("cw", true);
  cwSpeed = cwPrefs.getInt("speed", DEFAULT_WPM);
  cwTone = cwPrefs.getInt("tone", TONE_SIDETONE);
  cwKeyType = (KeyType)cwPrefs.getInt("keytype", KEY_IAMBIC_B);
  cwPrefs.end();

  // Validate settings
  if (cwSpeed < WPM_MIN) cwSpeed = WPM_MIN;
  if (cwSpeed > WPM_MAX) cwSpeed = WPM_MAX;
  if (cwTone < 400) cwTone = 400;
  if (cwTone > 1200) cwTone = 1200;

  Serial.print("CW Settings loaded: ");
  Serial.print(cwSpeed);
  Serial.print(" WPM, ");
  Serial.print(cwTone);
  Serial.print(" Hz, Key type: ");
  Serial.println(cwKeyType);
}

// Save CW settings to flash
void saveCWSettings() {
  cwPrefs.begin("cw", false);
  cwPrefs.putInt("speed", cwSpeed);
  cwPrefs.putInt("tone", cwTone);
  cwPrefs.putInt("keytype", (int)cwKeyType);
  cwPrefs.end();

  Serial.println("CW Settings saved");
}

// Start CW settings mode
void startCWSettings(Adafruit_ST7789 &display) {
  cwSettingSelection = 0;
  drawCWSettingsUI(display);
}

// Draw CW settings UI
void drawCWSettingsUI(Adafruit_ST7789 &display) {
  // Clear screen (preserve header)
  display.fillRect(0, 42, SCREEN_WIDTH, SCREEN_HEIGHT - 42, COLOR_BACKGROUND);

  // Title
  display.setTextSize(1);
  display.setTextColor(ST77XX_CYAN);
  display.setCursor(10, 55);
  display.print("CW Settings:");

  // Setting 0: Speed (WPM)
  int yPos = 80;
  bool isSelected = (cwSettingSelection == 0);

  if (isSelected) {
    display.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 28, 0x249F);
  }

  display.setTextSize(2);
  display.setTextColor(isSelected ? ST77XX_WHITE : ST77XX_CYAN);
  display.setCursor(10, yPos + 5);
  display.print("Speed:");

  display.setTextColor(isSelected ? ST77XX_WHITE : ST77XX_GREEN);
  display.setCursor(140, yPos + 5);
  display.print(cwSpeed);
  display.print(" WPM");

  // Setting 1: Tone (Hz)
  yPos += 35;
  isSelected = (cwSettingSelection == 1);

  if (isSelected) {
    display.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 28, 0x249F);
  }

  display.setTextSize(2);
  display.setTextColor(isSelected ? ST77XX_WHITE : ST77XX_CYAN);
  display.setCursor(10, yPos + 5);
  display.print("Tone:");

  display.setTextColor(isSelected ? ST77XX_WHITE : ST77XX_GREEN);
  display.setCursor(140, yPos + 5);
  display.print(cwTone);
  display.print(" Hz");

  // Setting 2: Key Type
  yPos += 35;
  isSelected = (cwSettingSelection == 2);

  if (isSelected) {
    display.fillRect(5, yPos - 2, SCREEN_WIDTH - 10, 28, 0x249F);
  }

  display.setTextSize(2);
  display.setTextColor(isSelected ? ST77XX_WHITE : ST77XX_CYAN);
  display.setCursor(10, yPos + 5);
  display.print("Key:");

  display.setTextColor(isSelected ? ST77XX_WHITE : ST77XX_GREEN);
  display.setCursor(140, yPos + 5);
  if (cwKeyType == KEY_STRAIGHT) {
    display.print("Straight");
  } else if (cwKeyType == KEY_IAMBIC_A) {
    display.print("Iambic A");
  } else {
    display.print("Iambic B");
  }

  // Draw footer instructions
  display.setTextSize(1);
  display.setTextColor(COLOR_WARNING);
  String footerText = "Up/Down: Select  Left/Right: Adjust  ESC: Back";

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(footerText, 0, 0, &x1, &y1, &w, &h);
  int centerX = (SCREEN_WIDTH - w) / 2;
  display.setCursor(centerX, SCREEN_HEIGHT - 12);
  display.print(footerText);
}

// Handle CW settings input
int handleCWSettingsInput(char key, Adafruit_ST7789 &display) {
  bool changed = false;

  if (key == KEY_UP) {
    if (cwSettingSelection > 0) {
      cwSettingSelection--;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      drawCWSettingsUI(display);
    }
    return 1;
  }
  else if (key == KEY_DOWN) {
    if (cwSettingSelection < CW_SETTINGS_COUNT - 1) {
      cwSettingSelection++;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      drawCWSettingsUI(display);
    }
    return 1;
  }
  else if (key == KEY_LEFT) {
    // Decrease value
    if (cwSettingSelection == 0) {
      // Speed
      if (cwSpeed > WPM_MIN) {
        cwSpeed--;
        changed = true;
      }
    } else if (cwSettingSelection == 1) {
      // Tone
      if (cwTone > 400) {
        cwTone -= 50;
        changed = true;
      }
    } else if (cwSettingSelection == 2) {
      // Key type
      if (cwKeyType == KEY_IAMBIC_B) {
        cwKeyType = KEY_IAMBIC_A;
        changed = true;
      } else if (cwKeyType == KEY_IAMBIC_A) {
        cwKeyType = KEY_STRAIGHT;
        changed = true;
      }
    }

    if (changed) {
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      saveCWSettings();
      drawCWSettingsUI(display);
    }
    return 1;
  }
  else if (key == KEY_RIGHT) {
    // Increase value
    if (cwSettingSelection == 0) {
      // Speed
      if (cwSpeed < WPM_MAX) {
        cwSpeed++;
        changed = true;
      }
    } else if (cwSettingSelection == 1) {
      // Tone
      if (cwTone < 1200) {
        cwTone += 50;
        changed = true;
      }
    } else if (cwSettingSelection == 2) {
      // Key type
      if (cwKeyType == KEY_STRAIGHT) {
        cwKeyType = KEY_IAMBIC_A;
        changed = true;
      } else if (cwKeyType == KEY_IAMBIC_A) {
        cwKeyType = KEY_IAMBIC_B;
        changed = true;
      }
    }

    if (changed) {
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
      saveCWSettings();
      drawCWSettingsUI(display);
    }
    return 1;
  }
  else if (key == KEY_ESC) {
    return -1;  // Exit CW settings
  }

  return 0;
}

#endif // SETTINGS_CW_H
