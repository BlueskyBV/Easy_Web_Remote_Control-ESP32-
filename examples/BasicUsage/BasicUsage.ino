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
  Serial.println("forward");
  delay(100);
}

void backward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  Serial.println("backward");
  delay(100);
}

void left() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  Serial.println("left");
  delay(100);
}

void right() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  Serial.println("right");
  delay(100);
}

void stopCar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  Serial.println("stop");
  delay(100);
}

void setup() {
  Serial.begin(115200);

  // Motor pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  stopCar(); // Make sure motors are off at boot

//   controller.setAPConfig(
//     IPAddress(10,10,10,1),    // AP IP (ESP32)
//     IPAddress(10,10,10,1),    // Gateway (same as AP IP)
//     IPAddress(255,255,255,0)  // Subnet
// ); --> Use this method to change and customize the address you need to connect to in your browser in AP mode to access the ESP32
// Clients connect to the AP SSID and then browse to http://10.10.10.1/

  // controller.setSTAStatic(
  //   IPAddress(192,168,0,125), // Device IP (pick one NOT already used)
  //   IPAddress(192,168,0,1),   // Your router
  //   IPAddress(255,255,255,0), // Subnet
  //   IPAddress(192,168,0,1),  // DNS1
  //   IPAddress(1, 1, 1, 1)    // DNS2 (optional)
  // ); --> Use this method to change the address you need to connect to in STA mode to access the ESP32 based on your device's IP, wifi's IP, DNS1 and DNS2 (OPTIONAL! Only if you have a DNS2. If no, skip it)

  // controller.setHostname("EasyRemote"); --> Use this method to change the host name of your ESP32 when it connects to your wifi in the wifi list

  controller.beginAP("RC_Car", "12345678"); //Use this method to begin the AP mode of the ESP32 and initialize the /ws server. Full freedom over WiFi name and password
  //controller.beginSTA("TP-Link_77D0", "14638152"); --> Use this method to begin the STA mode of the ESP32 and initialise the /ws server. Put as parameters the name and password of the wifi you want the ESP32 to connect to. You can also put a 3rd parameter which will be the number of miliseconds you want the trial of connectivity to expire if it can't connect to your wifi router. The default is 10000 ms. Skip this parameter if you want as it is only optional
  // controller.beginDual("RC_Car", "12345678", "TP-Link_77D0", "14638152") --> Use this method to begin the Dual mode of the ESP32 and initialise the /ws server. Input the AP and STA names and passwords. AP parameters first and STA parameters second. The connectivity expiration parameter is still in this method too and it is also optional so skip it if you don't want to change it

  // Attach commands
  controller.onFront(forward); // Attach the "forward()" method you previously created to the up arrow button
  controller.onBack(backward); // Attach the "backward()" method you previously created to the down arrow button
  controller.onLeft(left); // Attach the "left()" method you previously created to the left arrow button
  controller.onRight(right); // Attach the "right()" method you previously created to the right arrow button
  controller.onStop(stopCar); // Attach the "stop()" method you previously created to the middle button. VERY IMPORTANT! YOUR CUSTOM METHOD YOU ATTACH USING onStop() SHOULD BE THE METHOD YOU WANT TO CALL TO STOP ALL MECHANISMS THAT YOU ARE CONTROLLING AS IT IS CALLED AUTOMATICALLY AT THE END OF ALL OTHER INPUTS TO AT LEAST THEORETICALLY STOP THE MECHANISMS NATURALLY

  // Other set-up methods the library features (should be called in setup)
  //Button function configuration
  controller.addActionTimer("forward", 3000); // Method to be able for how long the attached function each button has runs for via button ID and a duration in miliseconds. addActionTimer([Button_ID], [Action Duration]); Use -1 as duration for infinite duration (requires manual stop). Use 0 for default run function while button is held functionality (already preset default button behaviour for all buttons)
  controller.setTaps("forward", 3); // Method to be able to set how many times a button needs to be tapped on in order for it to run its attached function. The button affected by this method should have an action timer set beforehand (using setActionTimer() method) to be able for the button's function to run for a preset amount of time. If the affected button does not have an already set action timer, its function will run infinitely once activated untill manual stop. Use 0 for the default
  controller.setHold("backward", 2000); // Method to be able to set for how long a button needs to be held down before executing its attached function. Use 0 for the default preset
  controller.setDelay("left", 2000); // Method to be able to set the delay that a certain button has before executing its attached function. Use 0 for the default no delay preset

  //Slider control
  //THE SLIDER CAN BE USED IN CODE TO ESTABILISH THE PWM OUTPUT OF YOUR SYSTEM! RANGES BETWEEN 0 AND 225
  controller.showSlider(true); //Method to show/hide the PWM slider in the web interface. The default is already true. Can be turned of using this method and setting it to false
  controller.setInitialPWM(25); // Method to set beforehand the initial PWM output on the web interface slider
  controller.getPWM(); // Returns the int value of the slider representing your PWM output. If coded right, can be used as the actual PWM output of your system
}

void loop() {
  controller.update(); // Method called in loop to continue reading inputs from buttons on the web interface
}