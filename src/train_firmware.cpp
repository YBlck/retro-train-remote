#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// DRV8833 driver pins (safe GPIOs without boot-strapping conflicts)
const int IN1_PIN = 4;
const int IN2_PIN = 5;

// Apply motor speed based on state (-1 to 3)
void applyMotorSpeed(int8_t state) {
  switch (state) {
    case 3: // FORWARD Speed 3 (Fast ~3.0V PWM)
      analogWrite(IN1_PIN, 140);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: FORWARD [ Fast / Speed 3 ]");
      break;

    case 2: // FORWARD Speed 2 (Medium ~2.3V PWM)
      analogWrite(IN1_PIN, 110);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: FORWARD [ Medium / Speed 2 ]");
      break;

    case 1: // FORWARD Speed 1 (Slow ~1.6V PWM)
      analogWrite(IN1_PIN, 80);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: FORWARD [ Slow / Speed 1 ]");
      break;

    case -1: // REVERSE (Hold mode PWM)
      analogWrite(IN1_PIN, 0);
      analogWrite(IN2_PIN, 80);
      Serial.println("Motor: REVERSE [ Hold Active ]");
      break;

    case 0: // STOP
    default:
      analogWrite(IN1_PIN, 0);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: STOPPED");
      break;
  }
}

// Callback signature for Arduino ESP32 Core v2.x (v2.0.17)
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len > 0) {
    int8_t speedState = (int8_t)incomingData[0];
    applyMotorSpeed(speedState);
  }
}

void setup() {
  // Force driver pins LOW immediately to prevent motor spin during boot
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);

  Serial.begin(115200);

  // Enable Wi-Fi in Station mode
  WiFi.mode(WIFI_STA);

  // Radio Optimizations to remove lag and prevent overheating:
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Lock Wi-Fi channel
  esp_wifi_set_ps(WIFI_PS_NONE);                  // Disable sleep mode for instant response
  esp_wifi_set_max_tx_power(32);                 // Lower TX power (~8dBm) to cool MCU down

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW!");
    return;
  }

  // Register ESP-NOW receive callback
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Optimized Train Motor Receiver Ready.");
}

void loop() {
  // Empty loop - incoming packets are handled asynchronously via interrupt callback
}
