// EXAMPLE CODE FOR A MODIFIED SIMPLE REMOTE CONTROLED CAR USING ESP32 SHOWCASING SYNTAX AND ALL BUILT-IN FUNCTIONALITIES

#include <EasyWebRemoteControl.h> // Include the library (MANDATORY)

EasyWebRemoteControl controller; // Create your own instance that you'll use to call the methods inside the library. That way, users have a bit more freedom on how they call them (MANDATORY)

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
  Serial.begin(115200); //Set Serial baud rate

  // Motor pins bheaviours
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  stopCar(); // Make sure motors are off at boot


  //LIBRARY USAGE

  // controller.setAPConfig(
  //   IPAddress(10,10,10,1),    // AP IP (ESP32)
  //   IPAddress(10,10,10,1),    // Gateway (same as AP IP)
  //   IPAddress(255,255,255,0)  // Subnet
  // ); //Use this method to change and customize the address you need to connect to in your browser in AP mode to access the ESP32 (OPTIONAL)
// In this example, clients connect to the AP SSID and then browse to http://10.10.10.1/
//The default address that you need to browse to in your browser is http://192.168.4.1/

  // controller.setSTAStatic(
  //   IPAddress(192,168,0,125), // Device IP (pick one NOT already used)
  //   IPAddress(192,168,0,1),   // Your router
  //   IPAddress(255,255,255,0), // Subnet
  //   IPAddress(192,168,0,1),  // DNS1
  //   IPAddress(1, 1, 1, 1)    // DNS2 (optional)
  // ); //Use this method to change the address you need to connect to in STA mode to access the ESP32 based on your device's IP, wifi's IP, DNS1 and DNS2 (OPTIONAL! Only if you have a DNS2. If no, skip it) (OPTIONAL)
  //In this example, clients connect to the wifi they set up the ESP32 to connect to then browse to their device's IP they set in the method. The one in the example might not work. You have to find your own device's IP
  //If you choose not to use this method, the address you'll need to connect to after you are also connected to the wifi you wanted the ESP32 to connect to will be displayed in the serial monitor for you

  //controller.setHostName("Remote Car"); //Use this method to change the host name of your ESP32 when it connects to your wifi in the wifi list (OPTIONAL)

  //Use only 1 of these 3 methods to initialize web connection
  //controller.beginAP("RC_Car", "12345678"); //Use this method to begin the AP mode of the ESP32 and initialize the /ws server. Full freedom over WiFi name and password. Password must be at least 8 characters long (MANDATORY)
  //controller.beginSTA("TP-Link_77D0", "14638152"); //Use this method to begin the STA mode of the ESP32 and initialise the /ws server. Put as parameters the name and password of the wifi you want the ESP32 to connect to. You can also put a 3rd parameter which will be the number of miliseconds you want the trial of connectivity to expire if it can't connect to your wifi router. The default is 10000 ms. Skip this parameter if you want as it is only optional (MANDATORY)
  controller.beginDual("RC_Car", "12345678", "TP-Link_77D0", "14638152", 20000); //Use this method to begin the Dual mode of the ESP32 and initialise the /ws server. Input the AP and STA names and passwords. AP mode password must be at least 8 characters long. AP parameters first and STA parameters second. The connectivity expiration parameter is still in this method too and it is also optional so skip it if you don't want to change it (MANDATORY)

  // Attach commands (Mandatory if you want functionality on each button. If you don't want functionality on some button, it's optional, Assigning an onStop functionality is highly recommended!)
  controller.onFront(forward); // Attach the "forward()" method you previously created to the up arrow button (MANDATORY/OPTIONAL)
  controller.onBack(backward); // Attach the "backward()" method you previously created to the down arrow button (MANDATORY/OPTIONAL)
  controller.onLeft(left); // Attach the "left()" method you previously created to the left arrow button (MANDATORY/OPTIONAL)
  controller.onRight(right); // Attach the "right()" method you previously created to the right arrow button (MANDATORY/OPTIONAL)
  controller.onStop(stopCar); // Attach the "stop()" method you previously created to the middle button. VERY IMPORTANT! YOUR CUSTOM METHOD YOU ATTACH USING onStop() SHOULD BE THE METHOD YOU WANT TO CALL TO STOP ALL MECHANISMS THAT YOU ARE CONTROLLING AS IT IS CALLED AUTOMATICALLY AT THE END OF ALL OTHER INPUTS TO AT LEAST THEORETICALLY STOP THE MECHANISMS NATURALLY (MANDATORY/OPTIONAL RECOMMENDED)

  // Other set-up methods the library features (should be called in setup)
  //Button function configuration
  controller.addActionTimer("front", 3000); // Method to be able for how long the attached function each button has runs for via button ID and a duration in miliseconds. addActionTimer([Button_ID], [Action Duration]); Use -1 as duration for infinite duration (requires manual stop). Use 0 for default run function while button is held functionality (already preset default button behaviour for all buttons) (OPTIONAL)
  controller.setTaps("front", 3); // Method to be able to set how many times a button needs to be tapped on in order for it to run its attached function. The button affected by this method should have an action timer set beforehand (using setActionTimer() method) to be able for the button's function to run for a preset amount of time. If the affected button does not have an already set action time,its function will run infinitely once activated untill manual stop.Use 0 for the default (OPTIONAL)
  controller.setHold("back", 2000); // Method to be able to set for how long a button needs to be held down before executing its attached function. Use 0 for the default preset (OPTIONAL)
  controller.setDelay("left", 2000); // Method to be able to set the delay that a certain button has before executing its attached function. Use 0 for the default no delay preset (OPTIONAL)
  
  //Slider control
  //THE SLIDER CAN BE USED IN CODE TO ESTABILISH THE PWM OUTPUT OF YOUR SYSTEM! RANGES BETWEEN 0 AND 225! DEFAULT IS 0!
  controller.showSlider(true); //Method to show/hide the PWM slider in the web interface. The default is already true. Can be turned of using this method and setting it to false (OPTIONAL)
  controller.setInitialPWM(25); // Method to set beforehand the initial PWM output on the web interface slider (OPTIONAL)
  controller.getPWM(); // Returns the int value of the slider representing your PWM output. If coded right, can be used as the actual PWM output of your system (OPTIONAL)

  //Auto_Recovery control (HIGHLY RECOMMENDED)
  controller.enableAutoRecovery(true); //Method to enable/disable auto-recovery. Default is true (OPTIONAL RECOMMENDED)
  controller.setAutoRecoveryTimings(
    60000,   // retryDurationMs (0 means it doesn't reconnect. -1 means it tries forever) (default is 10000)
    0,       // rebootAfterMs (0 means disabled) (default is 0)
    5000,    // checkIntervalMs (default is 5000)
    30000,   // reconnectAfterMs (start trying after 30s offline) (default is 30000)
    5000     // reconnectPeriodMs (every 5s) (default is 5000)
  ); //Method to customize the timings of the auto recovery functionality (OPTIONAL)

  //Video functionalities control
  // controller.enableVideo(true);  //Method to enable/disable the video box on the website and and the video functionalities (default is false) (OPTIONAL/MANDATORY if using video functionalities)
  // controller.setSnapshotFPS(30); //Method to set the refreshrate (per second) of your personal connected camera's snapshots (default is 5) (OPTIONAL)
  // controller.pauseSnapshots(false); //Method to pause/unpause the snapshots stream (OPTIONAL) (default is false)
  // controller.setVideoFrameProvider(...) //Method to register the function that supplies frames for the /snapshot HTTP route, the same way you set the functions for each button individualy. Without this, the endpoint returns 204 (No Content) (MANDATORY if using video functionalities/OPTIONAL) 
}

void loop() {
  controller.update(); // Method called in loop to continue reading inputs from buttons on the web interface and keep all functionalities going (MANDATORY)
}

//This example shows all of the functionalities of the Easy_Web_Remote_Control library, what they do, how to implement them and wether they are mandatory for the basic functionality of your program using this library (have "MANDATORY" at the end of the commentary line), or if they are optional and add more functionality (have "OPTIONAL" at the end of their commentary line) and/or if they are recommended even if they are just optional.
//Methods that are deemed mandatory in this example have no defaults. You have to call and set them up (if needed to be set up) manually! That's why they are mandatory!
//This should help all of you understand how to use the library and how to maximise your ESP32 based projects using it.
//For more information, please also check the README file.


// Button IDs: Front Button --> "front"
// 	           Back Button --> "back"
// 	           Left Button --> "left"
//     	       Right Button --> "right"
// 	           Middle Button (Stop Button) --> "stop"