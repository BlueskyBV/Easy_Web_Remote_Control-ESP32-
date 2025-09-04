// EXAMPLE CODE FOR A MODIFIED SIMPLE REMOTE CONTROLED CAR USING ESP32 SHOWCASING SYNTAX AND ALL BUILT-IN FUNCTIONALITIES

#include <EasyWebRemoteControl.h> // Include the library

EasyWebRemoteControl controller; // Create your own instance that you'll use to call the methods inside the library. That way, users have a bit more freedom on how they call them

// Motor driver pins
const int IN1 = 1; // Left motor1 forward
const int IN2 = 2; // Left motor1 backward
const int IN3 = 3; // Right motor1 forward
const int IN4 = 4; // Right motor1 backward

// Functions to move car
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
  digitalWrite(IN2, HIGH);  // Left motor backward
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);   // Right motor forward
}

void right() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);   // Left motor forward
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);  // Right motor backward
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

  stopCar(); // Make sure motors are off at boot

  controller.begin("RC_Car", "12345678"); // Start WiFi + Web control. Full freedom over WiFi name and password

  // Attach commands
  controller.onForward(forward); // Attach the "forward()" method you previously created to the up arrow button"
  controller.onBackward(backward); // Attach the "backward()" method you previously created to the down arrow button"
  controller.onLeft(left); // Attach the "left()" method you previously created to the left arrow button"
  controller.onRight(right); // Attach the "right()" method you previously created to the right arrow button"
  controller.onStop(stopCar); // Attach the "stop()" method you previously created to the middle button. VERY IMPORTANT! YOUR CUSTOM METHOD YOU ATTACH USING onStop() SHOULD BE THE METHOD YOU WANT TO CALL TO STOP ALL MECHANISMS THAT YOU ARE CONTROLLING AS IT IS CALLED AUTOMATICALLY AT THE END OF ALL OTHER INPUTS TO AT LEAST THEORETICALLY STOP THE MECHANISMS NATURALLY"

  // Other set-up methods the library features (should be called in setup)
  //Button function configuration
  controller.addActionTimer("forward", 3000); // Method to be able for how long the attached function each button has runs for via button ID and a duration in miliseconds. addActionTimer([Button_ID], [Action Duration]); Use -1 as duration for infinite duration (requires manual stop). Use 0 for default run function while button is held functionality (already preset default button behaviour for all buttons)
  controller.setTaps("forward", 3); // Method to be able to set how many times a button needs to be tapped on in order for it to run its attached function. The button affected by this method should have an action timer set beforehand (using setActionTimer() method) to be able for the button's function to run for a preset amount of time. If the affected button does not have an already set action timer, its function will run infinitely once activated untill manual stop. Use 0 for the default
  controller.setHold("backward", 2000); // Method to be able to set for how long a button needs to be held down before executing its attached function. Use 0 for the default preset
  controller.setDelay("left", 2000); // Method to be able to set the delay that a certain button has before executing its attached function. Use 0 for the default no delay preset

  //Slider control
  //THE SLIDER CAN BE USED IN CODE TO ESTABILISH THE PWM OUTPUT OF YOUR SYSTEM! RANGES BETWEEN 0 AND 225
  //The "Speed" in the method names for the slider doesn't necesarily reffer to the speed or something. Just the PWM output of your system. Depending on your system, it could mean the speed of a motor, brightness of an LED or just the power delivered to something else. I chose a simpler name for the methods so it's easier and more straightforward for beginners
  controller.showwSlider(true); //Method to show/hide the PWM slider in the web interface. The default is already true. Can be turned of using this method and setting it to false
  controller.setInitialSpeed(25); // Method to set beforehand the initial PWM output on the web interface slider
  controller.getSpeed(); // Returns the int value of the slider representing your PWM output. If coded right, can be used as the actual PWM output of your system
}

void loop() {
  controller.update(); // Method called in loop to continue reading inputs from buttons on the web interface
}
