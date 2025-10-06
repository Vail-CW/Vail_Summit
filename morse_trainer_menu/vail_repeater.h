/*
 * Vail Repeater Module
 * WebSocket client for vail.woozle.org morse code repeater
 *
 * REQUIRED LIBRARIES (install via Arduino Library Manager):
 * 1. WebSockets by Markus Sattler
 * 2. ArduinoJson by Benoit Blanchon
 */

#ifndef VAIL_REPEATER_H
#define VAIL_REPEATER_H

#include <WiFi.h>
#include <WiFiClientSecure.h>

// Set to 1 if you have the libraries installed, 0 if not
#define VAIL_ENABLED 1

#if VAIL_ENABLED
  #include <WebSocketsClient.h>
  #include <ArduinoJson.h>
#endif

#include "config.h"
#include "settings_cw.h"

// Default channel - always defined
String vailChannel = "General";

#if VAIL_ENABLED

// SSL client for WebSocket
WiFiClientSecure wifiClient;

// Vail repeater state
enum VailState {
  VAIL_DISCONNECTED,
  VAIL_CONNECTING,
  VAIL_CONNECTED,
  VAIL_ERROR
};

// Vail globals
WebSocketsClient webSocket;
VailState vailState = VAIL_DISCONNECTED;
VailState lastVailState = VAIL_DISCONNECTED;
String vailServer = "vail.woozle.org";
int vailPort = 443;  // WSS (secure WebSocket)
int connectedClients = 0;
int lastConnectedClients = 0;
String statusText = "";
bool needsUIRedraw = false;

// Transmit state
bool vailIsTransmitting = false;
unsigned long vailTxStartTime = 0;
bool vailTxToneOn = false;
unsigned long vailTxElementStart = 0;
std::vector<uint16_t> vailTxDurations;
int64_t lastTxTimestamp = 0;  // Track our last transmission to filter echoes
int64_t vailToneStartTimestamp = 0;  // Timestamp when current tone started

// Keyer state for Vail (similar to practice mode)
bool vailDitPressed = false;
bool vailDahPressed = false;
bool vailKeyerActive = false;
bool vailSendingDit = false;
bool vailSendingDah = false;
bool vailInSpacing = false;
bool vailDitMemory = false;
bool vailDahMemory = false;
unsigned long vailElementStartTime = 0;
int vailDitDuration = 0;

// Receive state
struct VailMessage {
  int64_t timestamp;
  uint16_t clients;
  std::vector<uint16_t> durations;
};

std::vector<VailMessage> rxQueue;
unsigned long playbackDelay = 500;  // 500ms delay for network jitter
int64_t clockSkew = 0;  // Offset to convert millis() to server time

// Forward declarations
void startVailRepeater(Adafruit_ST7789 &display);
void drawVailUI(Adafruit_ST7789 &display);
int handleVailInput(char key, Adafruit_ST7789 &display);
void updateVailRepeater();
void connectToVail(String channel);
void disconnectFromVail();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void sendVailMessage(std::vector<uint16_t> durations, int64_t timestamp = 0);
void processReceivedMessage(String jsonPayload);
void playbackMessages();
int64_t getCurrentTimestamp();
void updateVailPaddles();

// Get current timestamp in milliseconds (Unix epoch)
int64_t getCurrentTimestamp() {
  // Get time from NTP if available, otherwise use millis() with offset
  struct timeval tv;
  gettimeofday(&tv, NULL);
  int64_t timestamp = (int64_t)(tv.tv_sec) * 1000LL + (int64_t)(tv.tv_usec / 1000);

  // If timestamp is unreasonably small, we don't have NTP time yet
  // Use server clock skew to estimate
  if (timestamp < 1000000000000LL) {
    // No valid time, use millis() + clock skew
    timestamp = (int64_t)millis() + clockSkew;
  }

  return timestamp;
}

// Forward declaration of drawHeader (defined in main .ino file)
void drawHeader();

// Start Vail repeater mode
void startVailRepeater(Adafruit_ST7789 &display) {
  vailState = VAIL_DISCONNECTED;
  statusText = "Enter channel name";
  vailIsTransmitting = false;
  rxQueue.clear();
  vailTxDurations.clear();

  // Initialize keyer state
  vailKeyerActive = false;
  vailInSpacing = false;
  vailDitMemory = false;
  vailDahMemory = false;
  vailDitDuration = DIT_DURATION(cwSpeed);

  // Redraw header with correct title
  drawHeader();

  drawVailUI(display);
}

// Connect to Vail repeater
void connectToVail(String channel) {
  vailChannel = channel;
  vailState = VAIL_CONNECTING;
  statusText = "Connecting...";

  Serial.print("Connecting to Vail repeater: ");
  Serial.println(channel);

  // WebSocket connection with subprotocol
  String path = "/chat?repeater=" + channel;

  Serial.println("WebSocket connecting...");
  Serial.print("URL: wss://");
  Serial.print(vailServer);
  Serial.print(":");
  Serial.print(vailPort);
  Serial.println(path);

  // Set event handler first
  webSocket.onEvent(webSocketEvent);

  // Enable debug output and heartbeat
  webSocket.enableHeartbeat(15000, 3000, 2);

  // Set authorization header with subprotocol
  webSocket.setExtraHeaders("Sec-WebSocket-Protocol: json.vail.woozle.org");

  // Simple beginSSL - library should handle SSL automatically
  webSocket.beginSSL(vailServer.c_str(), vailPort, path.c_str());

  // Set reconnect interval
  webSocket.setReconnectInterval(5000);

  Serial.println("WebSocket setup complete");
}

// Disconnect from Vail
void disconnectFromVail() {
  webSocket.disconnect();
  vailState = VAIL_DISCONNECTED;
  statusText = "Disconnected";
}

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected");
      vailState = VAIL_DISCONNECTED;
      statusText = "Disconnected";
      needsUIRedraw = true;
      break;

    case WStype_CONNECTED:
      {
        Serial.println("[WS] Connected");
        vailState = VAIL_CONNECTED;
        statusText = "Connected";
        needsUIRedraw = true;

        // Get the URL we connected to
        String url = String((char*)payload);
        Serial.print("[WS] Connected to: ");
        Serial.println(url);
      }
      break;

    case WStype_TEXT:
      Serial.printf("[WS] Received: %s\n", payload);
      processReceivedMessage(String((char*)payload));
      break;

    case WStype_ERROR:
      Serial.println("[WS] Error");
      vailState = VAIL_ERROR;
      statusText = "Connection error";
      break;

    case WStype_PING:
      Serial.println("[WS] Ping");
      break;

    case WStype_PONG:
      Serial.println("[WS] Pong");
      break;

    default:
      break;
  }
}

// Process received JSON message
void processReceivedMessage(String jsonPayload) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonPayload);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  VailMessage msg;
  msg.timestamp = doc["Timestamp"].as<int64_t>();
  msg.clients = doc["Clients"].as<uint16_t>();

  // Update client count and trigger UI redraw if changed
  if (connectedClients != msg.clients) {
    connectedClients = msg.clients;
    needsUIRedraw = true;
  }

  JsonArray durations = doc["Duration"];
  if (durations.size() > 0) {
    // Check if this is our own message echoed back (within 100ms tolerance)
    if (abs(msg.timestamp - lastTxTimestamp) < 100) {
      Serial.println("Ignoring echo of our own transmission");
      return;
    }

    for (uint16_t duration : durations) {
      msg.durations.push_back(duration);
    }

    // Add to receive queue with playback delay
    rxQueue.push_back(msg);

    Serial.print("Queued message: ");
    Serial.print(msg.durations.size());
    Serial.println(" elements");
  } else {
    // Empty duration = clock sync message
    // Calculate offset from server time to our millis()
    clockSkew = msg.timestamp - (int64_t)millis();
    Serial.print("Clock sync: server=");
    Serial.print((long)msg.timestamp);
    Serial.print(" millis=");
    Serial.print((long)millis());
    Serial.print(" skew=");
    Serial.print((long)clockSkew);
    Serial.println(" ms");
  }
}

// Send message to Vail repeater
void sendVailMessage(std::vector<uint16_t> durations, int64_t timestamp) {
  if (vailState != VAIL_CONNECTED) {
    Serial.println("Not connected to Vail");
    return;
  }

  StaticJsonDocument<512> doc;

  // Use provided timestamp (when tone started), or get current time if not provided
  if (timestamp == 0) {
    timestamp = getCurrentTimestamp();
  }
  doc["Timestamp"] = timestamp;
  doc["Clients"] = 0;  // Server will fill this in

  JsonArray durArray = doc.createNestedArray("Duration");
  for (uint16_t dur : durations) {
    durArray.add(dur);
  }

  String output;
  serializeJson(doc, output);

  Serial.print("Sending (ts=");
  Serial.print((long)timestamp);
  Serial.print("): ");
  Serial.println(output);

  // Remember this timestamp to filter out the echo
  lastTxTimestamp = timestamp;

  webSocket.sendTXT(output);
}

// Update Vail repeater (call in main loop)
void updateVailRepeater(Adafruit_ST7789 &display) {
  webSocket.loop();

  // Update paddle transmission
  updateVailPaddles();

  // Playback received messages
  playbackMessages();

  // Redraw UI if status changed
  if (needsUIRedraw) {
    drawVailUI(display);
    needsUIRedraw = false;
  }
}

// Straight key handler for Vail
void vailStraightKeyHandler() {
  bool ditPressed = (digitalRead(DIT_PIN) == PADDLE_ACTIVE);

  if (!vailIsTransmitting && ditPressed) {
    // Start transmission
    vailIsTransmitting = true;
    vailTxStartTime = millis();
    vailTxToneOn = true;
    vailTxElementStart = millis();
    vailTxDurations.clear();
    tone(BUZZER_PIN, cwTone);
  }

  if (vailIsTransmitting) {
    // State changed (tone -> silence or silence -> tone)
    if (ditPressed != vailTxToneOn) {
      unsigned long duration = millis() - vailTxElementStart;
      vailTxDurations.push_back((uint16_t)duration);
      vailTxElementStart = millis();
      vailTxToneOn = ditPressed;

      if (ditPressed) {
        tone(BUZZER_PIN, cwTone);
      } else {
        noTone(BUZZER_PIN);
      }
    }

    // End transmission after 3 dit units of silence (letter spacing)
    if (!ditPressed && (millis() - vailTxElementStart > (vailDitDuration * 3))) {
      unsigned long duration = millis() - vailTxElementStart;
      vailTxDurations.push_back((uint16_t)duration);
      sendVailMessage(vailTxDurations);
      vailIsTransmitting = false;
      vailTxDurations.clear();
      noTone(BUZZER_PIN);
    }
  }
}

// Iambic keyer handler for Vail
void vailIambicKeyerHandler() {
  unsigned long currentTime = millis();

  // If not actively sending or spacing, check for new input
  if (!vailKeyerActive && !vailInSpacing) {
    if (vailDitPressed || vailDitMemory) {
      // Start sending dit
      vailKeyerActive = true;
      vailSendingDit = true;
      vailSendingDah = false;
      vailInSpacing = false;
      vailElementStartTime = currentTime;
      vailToneStartTimestamp = getCurrentTimestamp();  // Capture when tone starts
      tone(BUZZER_PIN, cwTone);

      // Start new transmission if needed
      if (!vailIsTransmitting) {
        vailIsTransmitting = true;
        vailTxStartTime = millis();
        vailTxDurations.clear();
      }

      vailDitMemory = false;
    }
    else if (vailDahPressed || vailDahMemory) {
      // Start sending dah
      vailKeyerActive = true;
      vailSendingDit = false;
      vailSendingDah = true;
      vailInSpacing = false;
      vailElementStartTime = currentTime;
      vailToneStartTimestamp = getCurrentTimestamp();  // Capture when tone starts
      tone(BUZZER_PIN, cwTone);

      // Start new transmission if needed
      if (!vailIsTransmitting) {
        vailIsTransmitting = true;
        vailTxStartTime = millis();
        vailTxDurations.clear();
      }

      vailDahMemory = false;
    }
    // No activity - check if we should reset transmission state
    else if (vailIsTransmitting && (millis() - vailTxStartTime > 2000)) {
      // Reset transmission state after 2 seconds of inactivity
      vailIsTransmitting = false;
    }
  }
  // Currently sending an element
  else if (vailKeyerActive && !vailInSpacing) {
    unsigned long elementDuration = vailSendingDit ? vailDitDuration : (vailDitDuration * 3);

    // Continuously check for paddle input during element send
    if (vailDitPressed && vailDahPressed) {
      if (vailSendingDit) {
        vailDahMemory = true;
      } else {
        vailDitMemory = true;
      }
    }
    else if (vailSendingDit && vailDahPressed) {
      vailDahMemory = true;
    }
    else if (vailSendingDah && vailDitPressed) {
      vailDitMemory = true;
    }

    // Check if element is complete
    if (currentTime - vailElementStartTime >= elementDuration) {
      // Send tone immediately using the timestamp from when it started
      sendVailMessage({(uint16_t)elementDuration}, vailToneStartTimestamp);

      // Element complete, turn off tone and start spacing
      noTone(BUZZER_PIN);
      vailKeyerActive = false;
      vailSendingDit = false;
      vailSendingDah = false;
      vailInSpacing = true;
      vailElementStartTime = currentTime;
      vailTxStartTime = millis();  // Reset idle timer
    }
  }
  // In inter-element spacing
  else if (vailInSpacing) {
    // Continue checking paddles during spacing
    if (vailDitPressed && vailDahPressed) {
      vailDitMemory = true;
      vailDahMemory = true;
    }
    else if (vailDitPressed && !vailDitMemory) {
      vailDitMemory = true;
    }
    else if (vailDahPressed && !vailDahMemory) {
      vailDahMemory = true;
    }

    unsigned long spaceDuration = currentTime - vailElementStartTime;

    // Check if next element is starting (memory set)
    if ((vailDitMemory || vailDahMemory) && spaceDuration >= vailDitDuration) {
      // Don't send silences - just move to next element
      vailInSpacing = false;
      vailTxStartTime = millis();  // Reset idle timer
    }
    // No next element queued - check for longer pause (reset transmission state after 2 seconds)
    else if (!vailDitMemory && !vailDahMemory && spaceDuration >= 2000) {
      // End transmission (no need to send silence)
      vailInSpacing = false;
      vailIsTransmitting = false;
    }
  }
}

// Handle paddle input for transmission
void updateVailPaddles() {
  vailDitPressed = (digitalRead(DIT_PIN) == PADDLE_ACTIVE);
  vailDahPressed = (digitalRead(DAH_PIN) == PADDLE_ACTIVE);

  // Use keyer based on settings
  if (cwKeyType == KEY_STRAIGHT) {
    vailStraightKeyHandler();
  } else {
    vailIambicKeyerHandler();
  }
}

// Playback state machine variables
static bool isPlaying = false;
static size_t playbackIndex = 0;
static unsigned long playbackElementStart = 0;

// Playback received messages (non-blocking)
void playbackMessages() {
  // Don't play if transmitting
  if (vailIsTransmitting) {
    if (isPlaying) {
      // Stop playback if we started transmitting
      noTone(BUZZER_PIN);
      isPlaying = false;
    }
    return;
  }

  if (rxQueue.empty() && !isPlaying) return;

  int64_t now = getCurrentTimestamp();

  // Start playing if not already playing
  if (!isPlaying && !rxQueue.empty()) {
    VailMessage &msg = rxQueue[0];
    int64_t playTime = msg.timestamp + playbackDelay;

    Serial.print("Checking playback: now=");
    Serial.print((long)now);
    Serial.print(" playTime=");
    Serial.print((long)playTime);
    Serial.print(" diff=");
    Serial.println((long)(playTime - now));

    if (now >= playTime) {
      Serial.print("Starting playback of ");
      Serial.print(msg.durations.size());
      Serial.println(" elements");
      isPlaying = true;
      playbackIndex = 0;
      playbackElementStart = millis();

      // Start first element
      if (msg.durations.size() > 0) {
        Serial.print("First element duration: ");
        Serial.println(msg.durations[0]);
        tone(BUZZER_PIN, cwTone);  // First element is always a tone
      }
    }
  }

  // Continue playing current message
  if (isPlaying && !rxQueue.empty()) {
    VailMessage &msg = rxQueue[0];

    // Check if current element is done
    unsigned long elapsed = millis() - playbackElementStart;
    if (elapsed >= msg.durations[playbackIndex]) {
      // Move to next element
      playbackIndex++;

      if (playbackIndex >= msg.durations.size()) {
        // Message complete
        noTone(BUZZER_PIN);
        isPlaying = false;
        playbackIndex = 0;
        rxQueue.erase(rxQueue.begin());
        Serial.println("Playback complete");
      } else {
        // Start next element
        playbackElementStart = millis();

        Serial.print("Element ");
        Serial.print(playbackIndex);
        Serial.print(": ");
        Serial.print(msg.durations[playbackIndex]);
        Serial.print("ms ");

        if (playbackIndex % 2 == 0) {
          // Even index = tone
          Serial.println("TONE");
          tone(BUZZER_PIN, cwTone);
        } else {
          // Odd index = silence
          Serial.println("SILENCE");
          noTone(BUZZER_PIN);
        }
      }
    }
  }
}

// Draw Vail UI
void drawVailUI(Adafruit_ST7789 &display) {
  // Clear screen (preserve header)
  display.fillRect(0, 42, SCREEN_WIDTH, SCREEN_HEIGHT - 42, COLOR_BACKGROUND);

  // Main info card - modern rounded rect
  int cardX = 20;
  int cardY = 55;
  int cardW = SCREEN_WIDTH - 40;
  int cardH = 130;

  display.fillRoundRect(cardX, cardY, cardW, cardH, 12, 0x1082); // Dark blue fill
  display.drawRoundRect(cardX, cardY, cardW, cardH, 12, 0x34BF); // Light blue outline

  // Channel
  display.setTextSize(1);
  display.setTextColor(0x7BEF); // Light gray
  display.setCursor(cardX + 15, cardY + 20);
  display.print("Channel");

  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(2);
  display.setCursor(cardX + 15, cardY + 38);
  display.print(vailChannel);

  // Status
  display.setTextSize(1);
  display.setTextColor(0x7BEF); // Light gray
  display.setCursor(cardX + 15, cardY + 65);
  display.print("Status");

  display.setTextSize(1);
  display.setCursor(cardX + 15, cardY + 83);
  if (vailState == VAIL_CONNECTED) {
    display.setTextColor(ST77XX_GREEN);
    display.print("Connected");
  } else if (vailState == VAIL_CONNECTING) {
    display.setTextColor(ST77XX_YELLOW);
    display.print("Connecting...");
  } else if (vailState == VAIL_ERROR) {
    display.setTextColor(ST77XX_RED);
    display.print("Error");
  } else {
    display.setTextColor(ST77XX_RED);
    display.print("Disconnected");
  }

  // Speed
  display.setTextSize(1);
  display.setTextColor(0x7BEF); // Light gray
  display.setCursor(cardX + 15, cardY + 105);
  display.print("Speed");

  display.setTextColor(ST77XX_CYAN);
  display.setTextSize(1);
  display.setCursor(cardX + 70, cardY + 105);
  display.print(cwSpeed);
  display.print(" WPM");

  // Operators (only when connected)
  if (vailState == VAIL_CONNECTED) {
    display.setTextColor(0x7BEF); // Light gray
    display.setCursor(cardX + 170, cardY + 105);
    display.print("Ops");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(cardX + 210, cardY + 105);
    display.print(connectedClients);
  }

  // TX indicator on card
  if (vailIsTransmitting) {
    display.fillCircle(cardX + cardW - 25, cardY + 25, 8, ST77XX_RED);
    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(cardX + cardW - 65, cardY + 22);
    display.print("TX");
  }

  // Instructions
  display.setTextSize(1);
  display.setTextColor(0x7BEF); // Light gray
  display.setCursor(30, 200);
  display.print("Use paddle to transmit");

  // Footer with controls
  display.setTextColor(COLOR_WARNING);
  display.setTextSize(1);
  display.setCursor(10, SCREEN_HEIGHT - 12);
  display.print("\x18\x19 Chan  \x1B\x1A Spd  ESC Exit");
}

// Handle Vail input
int handleVailInput(char key, Adafruit_ST7789 &display) {
  if (key == KEY_ESC) {
    disconnectFromVail();
    return -1;  // Exit Vail mode
  }

  // Arrow Up/Down: Change channel
  if (key == KEY_UP) {
    // Cycle through channels: General, 1-10
    if (vailChannel == "General") {
      vailChannel = "1";
    } else {
      int currentChannel = vailChannel.toInt();
      if (currentChannel >= 1 && currentChannel < 10) {
        vailChannel = String(currentChannel + 1);
      } else {
        vailChannel = "General";  // Wrap around to General
      }
    }

    // Reconnect to new channel
    disconnectFromVail();
    connectToVail(vailChannel);
    needsUIRedraw = true;
    tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
    return 0;
  }

  if (key == KEY_DOWN) {
    // Cycle through channels: General, 1-10
    if (vailChannel == "General") {
      vailChannel = "10";  // Wrap around to 10
    } else {
      int currentChannel = vailChannel.toInt();
      if (currentChannel > 1 && currentChannel <= 10) {
        vailChannel = String(currentChannel - 1);
      } else {
        vailChannel = "General";  // Go back to General
      }
    }

    // Reconnect to new channel
    disconnectFromVail();
    connectToVail(vailChannel);
    needsUIRedraw = true;
    tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
    return 0;
  }

  // Arrow Left/Right: Adjust speed
  if (key == KEY_LEFT) {
    if (cwSpeed > 5) {
      cwSpeed--;
      vailDitDuration = DIT_DURATION(cwSpeed);
      saveCWSettings();
      needsUIRedraw = true;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
    }
    return 0;
  }

  if (key == KEY_RIGHT) {
    if (cwSpeed < 40) {
      cwSpeed++;
      vailDitDuration = DIT_DURATION(cwSpeed);
      saveCWSettings();
      needsUIRedraw = true;
      tone(BUZZER_PIN, TONE_MENU_NAV, BEEP_SHORT);
    }
    return 0;
  }

  return 0;
}

#else  // VAIL_ENABLED == 0

// Stub functions when libraries are not installed
void startVailRepeater(Adafruit_ST7789 &display) {
  display.fillRect(0, 42, SCREEN_WIDTH, SCREEN_HEIGHT - 42, COLOR_BACKGROUND);
  display.setTextSize(1);
  display.setTextColor(ST77XX_RED);
  display.setCursor(20, 100);
  display.print("Vail repeater disabled");
  display.setCursor(20, 120);
  display.print("Install required libraries:");
  display.setCursor(20, 140);
  display.print("1. WebSockets");
  display.setCursor(20, 155);
  display.print("   by Markus Sattler");
  display.setCursor(20, 175);
  display.print("2. ArduinoJson");
  display.setCursor(20, 190);
  display.print("   by Benoit Blanchon");
}

void drawVailUI(Adafruit_ST7789 &display) {
  startVailRepeater(display);
}

int handleVailInput(char key, Adafruit_ST7789 &display) {
  if (key == KEY_ESC) return -1;
  return 0;
}

void updateVailRepeater(Adafruit_ST7789 &display) {
  // Nothing to do
}

void connectToVail(String channel) {
  // Nothing to do
}

void disconnectFromVail() {
  // Nothing to do
}

#endif // VAIL_ENABLED

#endif // VAIL_REPEATER_H
