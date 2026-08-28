#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

const int IN1_PIN = 4;
const int IN2_PIN = 5;

void applyMotorSpeed(int8_t state) {
  switch (state) {
    case 3: // FORWARD Speed 3 (~3.0V)
      analogWrite(IN1_PIN, 140);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: FORWARD [ Fast ]");
      break;

    case 2: // FORWARD Speed 2 (~2.3V)
      analogWrite(IN1_PIN, 115);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: FORWARD [ Medium ]");
      break;

    case 1: // FORWARD Speed 1 (~1.6V)
      analogWrite(IN1_PIN, 90);
      analogWrite(IN2_PIN, 0);
      Serial.println("Motor: FORWARD [ Slow ]");
      break;

    case -1: // REVERSE (Hold mode ~2.3V)
      analogWrite(IN1_PIN, 0);
      analogWrite(IN2_PIN, 90);
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

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len > 0) {
    int8_t speedState = (int8_t)incomingData[0];
    applyMotorSpeed(speedState);
  }
}

void setup() {
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW!");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Train Motor Receiver Ready.");
}

void loop() {
  // Empty loop - incoming packets are processed asynchronously via interrupt callback
}
