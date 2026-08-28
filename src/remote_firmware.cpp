#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

const int BTN_UP_PIN   = 6;
const int BTN_DOWN_PIN = 7;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Speed States: -1 = REVERSE, 0 = STOP, 1..3 = FORWARD speeds
int8_t currentSpeedState = 0;

bool lastUpState   = HIGH;
bool lastDownState = HIGH;

void sendSpeedState(int8_t state) {
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&state, 1);
  if (result == ESP_OK) {
    Serial.printf("Sent Speed State: %d\n", state);
  } else {
    Serial.println("Error sending ESP-NOW packet!");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW!");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }

  Serial.println("Remote Ready. Holding DOWN from STOP will activate Reverse.");
}

void loop() {
  bool upPressed   = (digitalRead(BTN_UP_PIN) == LOW);
  bool downPressed = (digitalRead(BTN_DOWN_PIN) == LOW);

  // Edge detection for clicks (transition from HIGH to LOW)
  bool upClicked   = (upPressed && lastUpState == HIGH);
  bool downClicked = (downPressed && lastDownState == HIGH);

  int8_t nextState = currentSpeedState;

  // 1. Safety check: Both buttons pressed -> Emergency STOP
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
      nextState = 1; // Start moving forward
    } else if (downPressed) {
      nextState = -1; // HOLD DOWN to reverse
    }
  }
  // 4. Mode: Currently REVERSING (-1)
  else if (currentSpeedState == -1) {
    if (!downPressed || upPressed) {
      nextState = 0; // Release DOWN button -> STOP
    }
  }

  // Send state update only when state changes
  if (nextState != currentSpeedState) {
    currentSpeedState = nextState;
    sendSpeedState(currentSpeedState);
  }

  lastUpState   = !upPressed;
  lastDownState = !downPressed;

  delay(25); // Debounce delay
}
