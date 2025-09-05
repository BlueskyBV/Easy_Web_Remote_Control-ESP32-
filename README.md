Easy_Web_Remote_Control

Control devices via a web interface with ESP32.
A library to easily create a web-based remote control interface on ESP32 using WebSockets. Supports directional buttons, speed slider, and configurable actions.

Table of Contents:

-Features

-Requirements

-Installation

-Usage

-API Reference

-Examples

-License



-Features:

Web-based control of devices over Wi-Fi

Directional buttons: Forward, Backward, Left, Right, Stop

Optional PWM output slider (0–255)

Configurable action timers, taps, hold, and delay

Fully asynchronous using WebSockets

Works with ESP32 boards



-Requirements:

You need the following libraries installed in your Arduino IDE:

Library	Minimum Tested Version	Notes
ESP Async WebServer by ESP32Async
	1.2.3+	Required for the web server (latest version is recommended)
AsyncTCP by ESP32Async
	1.1.1+	Required (latest version is recommended)
WiFi	Comes with the ESP32 Arduino core	No need to install separately



-Installation
Arduino IDE Installation

Download the latest release either from the Arduino Library Manager or GitHub (if from GitHub, add the library in the  Arduino IDE manually)

Restart the Arduino IDE.



-Usage
Basic Setup
#include <EasyWebRemoteControl.h>

EasyWebRemoteControl remote;

void setup() {
    Serial.begin(115200);
    remote.begin("MyESP32", "password123");

    remote.onForward([](){ Serial.println("Forward pressed"); });
    remote.onBackward([](){ Serial.println("Backward pressed"); });
    remote.onLeft([](){ Serial.println("Left pressed"); });
    remote.onRight([](){ Serial.println("Right pressed"); });
    remote.onStop([](){ Serial.println("Stop pressed"); });

    remote.setInitialSpeed(128);
    remote.showSlider(true);
}

void loop() {
    remote.update();
}



-API Reference:

Initialization
EasyWebRemoteControl remote;
remote.begin(ssid, password);

Callbacks
remote.onForward(func);
remote.onBackward(func);
remote.onLeft(func);
remote.onRight(func);
remote.onStop(func);

Slider
int speed = remote.getSpeed();
remote.setInitialSpeed(128);
remote.showSlider(true);

Configuration
remote.addActionTimer("forward", 1000); // milliseconds
remote.setTaps("left", 2);
remote.setHold("right", 500); // ms
remote.setDelay("backward", 200); // ms



-Examples

The library comes with a BasicUsage example. Copy the folder to your Arduino examples directory or open it directly from the IDE:

BasicUsage: Demonstrates buttons, slider, and callbacks.




-License

MIT License. See LICENSE for details.



Notes for Users

Tested on ESP32 boards only.

Use recommended libraries.

Web interface runs at http://192.168.4.1/ by default in AP mode.

Use, change and customize the library based on your project's needs!