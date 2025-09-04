#include <EasyWebRemoteControl.h>

EasyWebRemoteControl controller;

// Motor driver pins
const int IN1 = 1; // Left motor1 forward
const int IN2 = 2; // Left motor1 backward
const int IN3 = 3; // Right motor1 forward
const int IN4 = 4; // Right motor1 backward

// --- Functions to move car ---
void forward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void backward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void left() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);  // left motor backward
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);   // right motor forward
}

void right() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);   // left motor forward
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);  // right motor backward
}

void stopCar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void setup() {
  Serial.begin(115200);

  // Motor pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  stopCar(); // make sure motors are off at boot

  // Start WiFi + Web control
  controller.begin("RC_Car", "12345678");

  // Attach commands
  controller.onForward(forward);
  controller.onBackward(backward);
  controller.onLeft(left);
  controller.onRight(right);
  controller.onStop(stopCar);

  Serial.println("RC Car ready! Connect to WiFi: RC_Car, password: 12345678");
}

void loop() {
  controller.update();
}
