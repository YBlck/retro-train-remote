#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "sound_data.h"

// DRV8833 driver pins
const int IN1_PIN = 4; // Motor IN1
const int IN2_PIN = 5; // Motor IN2
const int AUDIO_PIN     = 2; // DRV8833 IN3 (Audio Channel B+)
const int AUDIO_PIN_INV = 3; // DRV8833 IN4 (Audio Channel B-)

// LED pin
const int LED_PIN = 8;
const int LED_ON = HIGH;
const int LED_OFF = LOW;

// LEDC Channels Assignment
const int MOTOR_CH_1   = 0;
const int MOTOR_CH_2   = 1;
const int AUDIO_CH     = 2;
const int AUDIO_CH_INV = 3;

// Offset to skip header bytes if needed
const uint32_t AUDIO_START_OFFSET = 0;

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
volatile uint32_t soundIndex = AUDIO_START_OFFSET;
volatile int fadeVolume = 0; // 0 to 256 for smooth start/stop envelope
hw_timer_t *audioTimer = NULL;

// Hardware Timer ISR (8000 Hz)
void IRAM_ATTR onAudioTimer() {
  // 1. Smooth Fade-In / Fade-Out control (~3 ms transition)
  if (isAudioPlaying) {
    if (fadeVolume < 256) fadeVolume += 8;
  } else {
    if (fadeVolume > 0) fadeVolume -= 8;
  }

  // 2. BTL Audio playback
  if (fadeVolume > 0 && chugSoundLen > AUDIO_START_OFFSET) {
    if (soundIndex < AUDIO_START_OFFSET) {
      soundIndex = AUDIO_START_OFFSET;
    }

    uint32_t idx = soundIndex;
    uint16_t sample = chugSound[idx];

    // Smooth seam transition (crossfade last 32 samples with the beginning sample)
    if (idx >= chugSoundLen - 32) {
      uint32_t remaining = chugSoundLen - idx;
      uint16_t firstSample = chugSound[AUDIO_START_OFFSET];
      sample = (sample * remaining + firstSample * (32 - remaining)) / 32;
    }

    // Apply fade envelope (fast bitwise division by 256)
    sample = (sample * fadeVolume) >> 8;

    // Output BTL anti-phase signals
    ledcWrite(AUDIO_CH, (uint8_t)sample);
    ledcWrite(AUDIO_CH_INV, (uint8_t)(255 - sample));

    if (isAudioPlaying) {
      soundIndex++;
      if (soundIndex >= chugSoundLen) {
        soundIndex = AUDIO_START_OFFSET; // Loop back to audio start
      }
    }
  } else {
    // Output 0V on both pins during silence (0V differential, no DC current or heating)
    ledcWrite(AUDIO_CH, 0);
    ledcWrite(AUDIO_CH_INV, 0);
    soundIndex = AUDIO_START_OFFSET;
  }
}

int stateToPWM(int8_t state) {
  switch (state) {
    case 3:  return 140;
    case 2:  return 110;
    case 1:  return 80;
    case -1: return -110;
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
    
    // Smoothly ramp down audio in ISR
    isAudioPlaying = false;
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
  pinMode(AUDIO_PIN_INV, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  // Configure Motor PWM (1 kHz, 8-bit resolution)
  ledcSetup(MOTOR_CH_1, 1000, 8);
  ledcAttachPin(IN1_PIN, MOTOR_CH_1);
  ledcSetup(MOTOR_CH_2, 1000, 8);
  ledcAttachPin(IN2_PIN, MOTOR_CH_2);

  // Configure BTL Audio PWM (31.25 kHz, 8-bit resolution)
  ledcSetup(AUDIO_CH, 31250, 8);
  ledcAttachPin(AUDIO_PIN, AUDIO_CH);
  ledcSetup(AUDIO_CH_INV, 31250, 8);
  ledcAttachPin(AUDIO_PIN_INV, AUDIO_CH_INV);

  ledcWrite(AUDIO_CH, 0);
  ledcWrite(AUDIO_CH_INV, 0);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(32);

  if (esp_now_init() != ESP_OK) {
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // Hardware Timer configuration (8000 Hz)
  audioTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(audioTimer, &onAudioTimer, true);
  timerAlarmWrite(audioTimer, 125, true); // 125 ticks = 8000 Hz
  timerAlarmEnable(audioTimer);
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
