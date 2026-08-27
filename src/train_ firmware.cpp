#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// DRV8833 driver pins (safe GPIOs without boot-strapping issues)
const int IN1_PIN = 4;
const int IN2_PIN = 5;

// Callback function signature for Arduino ESP32 Core v2.x (v2.0.17)
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len > 0) {
    uint8_t command = incomingData[0];

    if (command == 1) {
      analogWrite(IN1_PIN, 100); // Drive forward
      analogWrite(IN2_PIN, 0);
      Serial.println("Command received: [ 1 ] -> Motor STARTED");
    } else {
      analogWrite(IN1_PIN, 0);   // Full stop
      analogWrite(IN2_PIN, 0);
      Serial.println("Command received: [ 0 ] -> Motor STOPPED");
    }
  }
}

void setup() {
  // Immediately force driver pins LOW to prevent motor spin during MCU boot
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);

  Serial.begin(115200);

  // Enable Wi-Fi in Station mode for ESP-NOW
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW!");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("ESP-NOW Train initialized. Waiting for remote signals...");
}

void loop() {
  // Empty loop - incoming packets are processed asynchronously via interrupt callback
}
