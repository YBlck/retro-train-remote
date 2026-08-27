#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// Built-in BOOT button on ESP32-C3 SuperMini
const int BTN_PIN = 9;

// Broadcast MAC address (sends packet to all listening devices)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t lastState = 255;

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  // ESP-NOW requires Wi-Fi in Station mode
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Помилка ініціалізації ESP-NOW!");
    return;
  }

 // Register broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Помилка додавання Peer!");
    return;
  }

  Serial.println("Пульт ESP-NOW запущено. Натисніть кнопку BOOT.");
}

void loop() {
// 1 = pressed (LOW), 0 = released (HIGH)
  uint8_t currentState = (digitalRead(BTN_PIN) == LOW) ? 1 : 0;

  // Send data only when button state changes
  if (currentState != lastState) {
    esp_err_t result = esp_now_send(broadcastAddress, &currentState, 1);
    
    if (result == ESP_OK) {
      Serial.printf("Sent state: %d\n", currentState);
    } else {
      Serial.println("Error sending ESP-NOW packet!");
    }
    
    lastState = currentState;
  }

  delay(20); // Debounce delay
}
