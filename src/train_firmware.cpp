#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "sound_data.h"

// DRV8833 driver pins
const int IN1_PIN = 4;
const int IN2_PIN = 5;
const int AUDIO_PIN = 2; // DRV8833 channel B

// LED pin
const int LED_PIN = 8;
const int LED_ON = HIGH;
const int LED_OFF = LOW;

// LEDC Channels Assignment
const int MOTOR_CH_1 = 0;
const int MOTOR_CH_2 = 1;
const int AUDIO_CH   = 2;

// motor speed state to PWM mapping
const int SPEED_1 = 80;   // Forward speed 1
const int SPEED_2 = 100;  // Forward speed 2
const int SPEED_3 = 120;  // Forward speed 3

unsigned long lastPacketTime = 0;
const unsigned long CONNECTION_TIMEOUT_MS = 3000;
unsigned long lastBlinkTime = 0;
bool ledState = LED_OFF;

int targetPWM = 0;
int currentPWM = 0;

unsigned long lastRampTime = 0;
const unsigned long RAMP_INTERVAL_MS = 20;
const int PWM_STEP = 5;

volatile bool isAudioPlaying = false;
volatile uint32_t soundIndex = 0;
hw_timer_t *audioTimer = NULL;

// Setup audio timer interrupt (8000 Hz)
void IRAM_ATTR onAudioTimer() {
  if (isAudioPlaying && chugSoundLen > 0) {
    ledcWrite(AUDIO_CH, chugSound[soundIndex]);
    soundIndex++;
    if (soundIndex >= chugSoundLen) {
      soundIndex = 0; // Loop sound
    }
  } else {
    ledcWrite(AUDIO_CH, 0);
  }
}

int stateToPWM(int8_t state) {
  switch (state) {
    case 3:  return SPEED_3;
    case 2:  return SPEED_2;
    case 1:  return SPEED_1;
    case -1: return -SPEED_1;
    case 0:
    default: return 0;
  }
}

void updateMotorHardware(int pwm) {
  if (pwm > 0) {
    // Forward
    ledcWrite(MOTOR_CH_1, pwm);
    ledcWrite(MOTOR_CH_2, 0);
    isAudioPlaying = true;
  } 
  else if (pwm < 0) {
    // Reverse
    ledcWrite(MOTOR_CH_1, 0);
    ledcWrite(MOTOR_CH_2, abs(pwm));
    isAudioPlaying = true;
  } 
  else {
    // Full stop
    ledcWrite(MOTOR_CH_1, 0);
    ledcWrite(MOTOR_CH_2, 0);
    isAudioPlaying = false;
    soundIndex = 0;
    ledcWrite(AUDIO_CH, 0);
  }
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len > 0) {
    lastPacketTime = millis();
    int8_t speedState = (int8_t)incomingData[0];
    targetPWM = stateToPWM(speedState);
  }
}

void setup() {
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  // Setup motor PWM (1 kHz, 8-bit)
  ledcSetup(MOTOR_CH_1, 1000, 8);
  ledcAttachPin(IN1_PIN, MOTOR_CH_1);
  ledcSetup(MOTOR_CH_2, 1000, 8);
  ledcAttachPin(IN2_PIN, MOTOR_CH_2);

  // Setup audio PWM (31.25 kHz, 8-bit)
  ledcSetup(AUDIO_CH, 31250, 8);
  ledcAttachPin(AUDIO_PIN, AUDIO_CH);
  ledcWrite(AUDIO_CH, 0);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(32);

  if (esp_now_init() != ESP_OK) {
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // Timer settings (8000 Hz)
  audioTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(audioTimer, &onAudioTimer, true);
  timerAlarmWrite(audioTimer, 125, true);
  timerAlarmEnable(audioTimer);

  isAudioPlaying = true;
}

void loop() {
  unsigned long now = millis();
  bool isConnected = (now - lastPacketTime <= CONNECTION_TIMEOUT_MS);

  if (isConnected) {
    digitalWrite(LED_PIN, LED_ON);
  } else {
    targetPWM = 0;
    if (now - lastBlinkTime >= 300) {
      lastBlinkTime = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
    }
  }

  if (now - lastRampTime >= RAMP_INTERVAL_MS) {
    lastRampTime = now;
    if (currentPWM != targetPWM) {
      if (currentPWM < targetPWM) {
        currentPWM += PWM_STEP;
        if (currentPWM > targetPWM) currentPWM = targetPWM;
      } 
      else if (currentPWM > targetPWM) {
        currentPWM -= PWM_STEP;
        if (currentPWM < targetPWM) currentPWM = targetPWM;
      }
      updateMotorHardware(currentPWM);
    }
  }
}
