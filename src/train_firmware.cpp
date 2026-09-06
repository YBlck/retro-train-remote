#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "sound_data.h"

// DRV8833 driver pins
const int IN1_PIN = 4; 
const int IN2_PIN = 5; 

// Audio BTL pins
const int AUDIO_PIN     = 2; 
const int AUDIO_PIN_INV = 3; 

// LED pin
const int LED_PIN = 8;
const int LED_ON  = HIGH;
const int LED_OFF = LOW;

// LEDC Channels Assignment
const int MOTOR_CH_1   = 0;
const int MOTOR_CH_2   = 1;
const int AUDIO_CH     = 2; 
const int AUDIO_CH_INV = 3; 

// Motor speed mapping
const int SPEED_1 = 180;   
const int SPEED_2 = 220;  
const int SPEED_3 = 255;  

// Audio pitch parameters
const int BASE_TICKS = 125;
const int MAX_TICKS  = 95;

volatile unsigned long lastPacketTime = 0;
const unsigned long CONNECTION_TIMEOUT_MS = 10000;
unsigned long lastBlinkTime = 0;
bool ledState = LED_OFF;

volatile int targetPWM = 0;
int currentPWM = 0;

unsigned long lastRampTime = 0;
const unsigned long RAMP_INTERVAL_MS = 20;
const int PWM_STEP = 5;

// Audio control flags
volatile bool isAudioPlaying = false;
volatile bool isHornPlaying = false;
volatile uint32_t soundIndex = 0;
volatile uint32_t hornIndex = 0;

hw_timer_t *audioTimer = NULL;
int lastAudioTicks = -1;

// Setup audio timer interrupt (8000 Hz base)
void IRAM_ATTR onAudioTimer() {
  uint8_t sample = 0;
  bool activeSound = false;

  // Priority 1: Horn
  if (isHornPlaying && hornSoundLen > 0) {
    sample = hornSound[hornIndex];
    hornIndex++;
    activeSound = true;

    if (hornIndex >= hornSoundLen) {
      hornIndex = 0;
      isHornPlaying = false;
    }
  } 
  // Priority 2: Motor sound (chug-chug)
  else if (isAudioPlaying && chugSoundLen > 0) {
    sample = chugSound[soundIndex];
    soundIndex++;
    activeSound = true;

    if (soundIndex >= chugSoundLen) {
      soundIndex = 0;
    }
  }

  if (activeSound) {
    ledcWrite(AUDIO_CH, sample);
    ledcWrite(AUDIO_CH_INV, 255 - sample);
  } else {
    ledcWrite(AUDIO_CH, 0);
    ledcWrite(AUDIO_CH_INV, 0);
  }
}

void updateAudioPitch(int pwm) {
  int absPwm = abs(pwm);
  int ticks = BASE_TICKS;

  if (absPwm > 0) {
    ticks = map(absPwm, SPEED_1, SPEED_3, BASE_TICKS, MAX_TICKS);
    ticks = constrain(ticks, MAX_TICKS, BASE_TICKS);
  }

  if (ticks != lastAudioTicks) {
    lastAudioTicks = ticks;
    timerAlarmWrite(audioTimer, ticks, true);
  }
}

int stateToPWM(int8_t state) {
  switch (state) {
    case 3:  return SPEED_3;
    case 2:  return SPEED_2;
    case 1:  return SPEED_1;
    case -1: return -SPEED_1 - 40;
    case 0:
    default: return 0;
  }
}

void updateMotorHardware(int pwm) {
  if (pwm > 0) {
    ledcWrite(MOTOR_CH_1, pwm);
    ledcWrite(MOTOR_CH_2, 0);
    isAudioPlaying = true;
  } 
  else if (pwm < 0) {
    ledcWrite(MOTOR_CH_1, 0);
    ledcWrite(MOTOR_CH_2, abs(pwm));
    isAudioPlaying = true;
  } 
  else {
    ledcWrite(MOTOR_CH_1, 0);
    ledcWrite(MOTOR_CH_2, 0);
    isAudioPlaying = false;
    soundIndex = 0;
  }

  updateAudioPitch(pwm);
}

// Receive data: 1st byte = speed, 2nd byte = horn button (1/0)
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len >= 1) {
    lastPacketTime = millis();
    int8_t speedState = (int8_t)incomingData[0];
    targetPWM = stateToPWM(speedState);
  }

  if (len >= 2) {
    bool hornState = (incomingData[1] == 1);
    // Start horn sound if button is pressed and it's not already playing
    if (hornState && !isHornPlaying) {
      hornIndex = 0;
      isHornPlaying = true;
    }
  }
}

void setup() {
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);
  pinMode(AUDIO_PIN_INV, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  // Setup motor PWM
  ledcSetup(MOTOR_CH_1, 1000, 8);
  ledcAttachPin(IN1_PIN, MOTOR_CH_1);
  ledcSetup(MOTOR_CH_2, 1000, 8);
  ledcAttachPin(IN2_PIN, MOTOR_CH_2);

  // Setup audio PWM BTL
  ledcSetup(AUDIO_CH, 31250, 8);
  ledcAttachPin(AUDIO_PIN, AUDIO_CH);
  ledcSetup(AUDIO_CH_INV, 31250, 8);
  ledcAttachPin(AUDIO_PIN_INV, AUDIO_CH_INV);

  ledcWrite(AUDIO_CH, 0);
  ledcWrite(AUDIO_CH_INV, 0);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(32);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
  }

  // Timer settings
  audioTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(audioTimer, &onAudioTimer, true);
  lastAudioTicks = BASE_TICKS;
  timerAlarmWrite(audioTimer, BASE_TICKS, true);
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
    int target = targetPWM;

    if (currentPWM != target) {
      // INSTANT START FROM ZERO: avoid motor stall at low PWM values
      if (currentPWM == 0) {
        if (target > 0) {
          currentPWM = SPEED_1;   // Jump directly to min speed forward
        } else if (target < 0) {
          currentPWM = -SPEED_1;  // Jump directly to min speed reverse
        }
      } 
      // SMOOTH RAMP BETWEEN SPEED STEPS OR SMOOTH STOP
      else {
        if (currentPWM < target) {
          currentPWM += PWM_STEP;
          if (currentPWM > target) currentPWM = target;
        } 
        else if (currentPWM > target) {
          currentPWM -= PWM_STEP;
          if (currentPWM < target) currentPWM = target;
        }
      }

      updateMotorHardware(currentPWM);
    }
  }
}
