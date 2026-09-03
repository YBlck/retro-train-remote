#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "sound_data.h"

// DRV8833 driver pins
const int IN1_PIN = 4;
const int IN2_PIN = 5;

const int AUDIO_PIN = 2; // DRV8833 channel B - B1IN - B2IN=GND

// LED pin for status indication
const int LED_PIN = 8;
const int LED_ON = HIGH;
const int LED_OFF = LOW;

unsigned long lastPacketTime = 0;
const unsigned long CONNECTION_TIMEOUT_MS = 2000; // 2 sec without packets = connection lost
unsigned long lastBlinkTime = 0;
bool ledState = LED_OFF;

// Positive = FORWARD, Negative = REVERSE, 0 = STOP
int targetPWM = 0;
int currentPWM = 0;

// Acceleration configuration
unsigned long lastRampTime = 0;
const unsigned long RAMP_INTERVAL_MS = 20; // Time between PWM steps (lower = faster acceleration)
const int PWM_STEP = 5; // PWM change per step (higher = faster acceleration)

// Audio playback state
volatile bool isAudioPlaying = false;
volatile uint32_t soundIndex = 0;
hw_timer_t *audioTimer = NULL;

// Timer interrupt: called 11025 times per second
void IRAM_ATTR onAudioTimer() {
  if (isAudioPlaying && chugSoundLen > 0) {
    // Output current sample to PWM (0..255)
    analogWrite(AUDIO_PIN, chugSound[soundIndex]);

    soundIndex++;
    if (soundIndex >= chugSoundLen) {
      soundIndex = 0; // Loop chugging sound
    }
  } else {
    analogWrite(AUDIO_PIN, 0); // Silence on stop
  }
}

// Map speed states (-1 to 3) to exact custom PWM values
int stateToPWM(int8_t state) {
  switch (state) {
    case 3:  return 140;  // FORWARD Speed 3 (Fast)
    case 2:  return 110;  // FORWARD Speed 2 (Medium)
    case 1:  return 80;   // FORWARD Speed 1 (Slow)
    case -1: return -110; // REVERSE (Hold mode)
    case 0:
    default: return 0;    // STOP
  }
}

// Update driver hardware outputs based on current PWM
void updateMotorHardware(int pwm) {
  if (pwm > 0) {
    // Forward
    analogWrite(IN1_PIN, pwm);
    analogWrite(IN2_PIN, 0);
    isAudioPlaying = true;
  } 
  else if (pwm < 0) {
    // Reverse
    analogWrite(IN1_PIN, 0);
    analogWrite(IN2_PIN, abs(pwm));
    isAudioPlaying = true;
  } 
  else {
    // Full stop
    analogWrite(IN1_PIN, 0);
    analogWrite(IN2_PIN, 0);
    isAudioPlaying = false;
    soundIndex = 0;
    analogWrite(AUDIO_PIN, 0);
  }
}

// ESP-NOW Receive Callback
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len > 0) {
    lastPacketTime = millis();

    int8_t speedState = (int8_t)incomingData[0];
    targetPWM = stateToPWM(speedState);
    Serial.printf("Command received: State [%d] -> Target PWM: %d\n", speedState, targetPWM);
  }
}

void setup() {
  // Force driver pins LOW immediately to prevent motor spin during MCU boot
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);

  // Audio pin init (31.25 kHz PWM frequency eliminates 1kHz high-pitch carrier noise)
  pinMode(AUDIO_PIN, OUTPUT);
  analogWriteFrequency(31250);
  analogWrite(AUDIO_PIN, 0);

  // LED pin init
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);

  // Radio Optimizations (Channel lock, NO sleep, low TX power)
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(32); // ~8dBm TX power for ultra low consumption

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW!");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // Audio timer setup (11025 Hz sampling rate)
  // 80 MHz / 80 = 1 MHz (1 us tick). 1000000 / 11025 ≈ 91 us period.
  audioTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(audioTimer, &onAudioTimer, true);
  timerAlarmWrite(audioTimer, 91, true);
  timerAlarmEnable(audioTimer);

  Serial.println("Train Controller Ready (Motor + Audio + Status LED).");
}

void loop() {
  unsigned long now = millis();

  // Connection control
  bool isConnected = (now - lastPacketTime <= CONNECTION_TIMEOUT_MS);

  if (isConnected) {
    digitalWrite(LED_PIN, LED_ON); // Signal present — LED ON
  } else {
    targetPWM = 0; // Emergency stop motor on signal loss
    
    // Blink LED (every 300 ms)
    if (now - lastBlinkTime >= 300) {
      lastBlinkTime = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
    }
  }

  // Smooth PWM acceleration/deceleration ramp ticker
  if (now - lastRampTime >= RAMP_INTERVAL_MS) {
    lastRampTime = now;

    if (currentPWM != targetPWM) {
      // Step currentPWM towards targetPWM
      if (currentPWM < targetPWM) {
        currentPWM += PWM_STEP;
        if (currentPWM > targetPWM) currentPWM = targetPWM;
      } 
      else if (currentPWM > targetPWM) {
        currentPWM -= PWM_STEP;
        if (currentPWM < targetPWM) currentPWM = targetPWM;
      }

      // Apply the newly calculated PWM to the driver pins
      updateMotorHardware(currentPWM);
    }
  }
}
