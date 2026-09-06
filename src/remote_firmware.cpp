#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

const int BTN_UP_PIN   = 7;
const int BTN_DOWN_PIN = 6;
const int BTN_HORN_PIN = 5;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Speed States:
// -1 = REVERSE (hold mode)
//  0 = STOP
//  1 = FORWARD Speed 1
//  2 = FORWARD Speed 2
//  3 = FORWARD Speed 3
int8_t currentSpeedState = 0;
bool lastHornState = false;

bool lastUpState   = HIGH;
bool lastDownState = HIGH;

// Heartbeat ticker
unsigned long lastSendTime = 0;
const unsigned long HEARTBEAT_MS = 500;

void sendControlPacket(int8_t speedState, uint8_t hornState) {
  uint8_t packet[2];
  packet[0] = (uint8_t)speedState;
  packet[1] = hornState;

  esp_err_t result = esp_now_send(broadcastAddress, packet, sizeof(packet));
  if (result != ESP_OK) {
    Serial.println("Error sending ESP-NOW packet!");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_HORN_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);

  // Radio Optimizations:
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Lock Wi-Fi channel
  esp_wifi_set_ps(WIFI_PS_NONE);                  // Disable sleep mode for instant response
  esp_wifi_set_max_tx_power(32);                  // Lower TX power (~8dBm)

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW!");
    return;
  }

  // Register broadcast peer on channel 1
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }

  Serial.println("Remote Ready with Horn support.");
}

void loop() {
  bool upPressed   = (digitalRead(BTN_UP_PIN) == LOW);
  bool downPressed = (digitalRead(BTN_DOWN_PIN) == LOW);
  bool hornPressed = (digitalRead(BTN_HORN_PIN) == LOW);

  // Edge detection for clicks (HIGH -> LOW transition)
  bool upClicked   = (upPressed && lastUpState == HIGH);
  bool downClicked = (downPressed && lastDownState == HIGH);

  int8_t nextState = currentSpeedState;

  // 1. Safety check: Both speed buttons pressed -> Emergency STOP
  if (upPressed && downPressed) {
    nextState = 0;
  }
  // 2. Mode: Currently moving FORWARD (1, 2, 3)
  else if (currentSpeedState > 0) {
    if (upClicked && currentSpeedState < 3) {
      nextState++; // Increase forward speed
    } else if (downClicked) {
      nextState--; // Decrease forward speed / stop
    }
  }
  // 3. Mode: Currently STOPPED (0)
  else if (currentSpeedState == 0) {
    if (upClicked) {
      nextState = 1; // Start moving forward at Speed 1
    } else if (downPressed) {
      nextState = -1; // Hold DOWN button for Reverse
    }
  }
  // 4. Mode: Currently REVERSING (-1)
  else if (currentSpeedState == -1) {
    if (!downPressed || upPressed) {
      nextState = 0; // Releasing DOWN button triggers STOP
    }
  }

  unsigned long now = millis();
  bool speedChanged = (nextState != currentSpeedState);
  bool hornChanged  = (hornPressed != lastHornState);

  // Send packet if speed state changed, horn state changed, or on heartbeat interval
  if (speedChanged || hornChanged || (now - lastSendTime >= HEARTBEAT_MS)) {
    currentSpeedState = nextState;
    lastHornState = hornPressed;

    sendControlPacket(currentSpeedState, hornPressed ? 1 : 0);
    lastSendTime = now;
  }

  lastUpState   = !upPressed;
  lastDownState = !downPressed;

  delay(25); // Debounce delay
}
