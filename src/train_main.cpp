#include <Arduino.h>

const int IN1 = 1; // GPIO 1
const int IN2 = 2; // GPIO 2

void stopMotor() {
  analogWrite(IN1, 0);
  analogWrite(IN2, 0);
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  stopMotor();
}

void loop() {
  delay(5000);

  analogWrite(IN2, 0);

  // 2V forward 5 sec
  analogWrite(IN2, 80);
  delay(5000);

  stopMotor();
  delay(2000);

  // 2.5V forward 5 sec
  analogWrite(IN2, 105);
  delay(5000);

  stopMotor();
  delay(2000);

  // 3V forward 5 sec
  analogWrite(IN2, 130);
  delay(5000);

  stopMotor();
  delay(2000);

  // 2V backward 5 sec
  analogWrite(IN1, 0);
  delay(5000);

  stopMotor();
}
