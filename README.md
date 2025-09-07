Easy_Web_Remote_Control by Colojoara Alexandru also known as BlueskyBV
Version 3.0.0

Control devices via a web interface with ESP32.
A library to easily create a web-based remote control interface on ESP32 using WebSockets. Supports directional buttons, PWM output slider, connection auto-recovery, universal video capabilities and configurable actions.

Table of Contents:

-Features

-Requirements

-Installation

-Usage

-API Reference

-Examples

-License



-Features:

Web-based control of devices over Wi-Fi (supports AP mode, STA mode and Dual mode).

Directional buttons: Front, Back, Left, Right, Stop.

Optional PWM output slider (0–255).

Configurable action timers, taps, hold, and delay.

Fully asynchronous using WebSockets.

Configurable and universal video support for cameras.

Has integrated connection auto-recovery.

Works with ESP32 boards.



-Requirements:

You need the following libraries installed in your Arduino IDE:

Library	Minimum Tested Versions:

ESP Async WebServer by ESP32Async
	3.0.0+	Required for the web server (latest version is recommended)
AsyncTCP by ESP32Async
	3.0.1+	Required (latest version is recommended)
Arduino ESP32 Boards core (Not a library. It is a board)
esp32 core (Not a library. It is a board)

!Board cores must be downloaded from the Board Manager inside the Arduino IDE!



-Installation
Arduino IDE Installation.

Download the latest release of the library either from the Arduino Library Manager or GitHub (if from GitHub, add the library in the Arduino IDE manually).

Restart the Arduino IDE.



-Usage
Basic Setup
#include <EasyWebRemoteControl.h>

EasyWebRemoteControl remote;

void setup() {
    Serial.begin(115200);
    remote.begin("MyESP32", "password123");

    remote.onFront([](){ Serial.println("Front pressed"); });
    remote.onBack([](){ Serial.println("Back pressed"); });
    remote.onLeft([](){ Serial.println("Left pressed"); });
    remote.onRight([](){ Serial.println("Right pressed"); });
    remote.onStop([](){ Serial.println("Stop pressed"); });

    remote.setInitialSpeed(128);
    remote.showSlider(true);

    remote.enableAutoRecovery(true);
}

void loop() {
    remote.update();
    int PWMOutput = remote.getPWM();
}



-API Reference:

Initialization
#include <EasyWebRemoteControl.h>
EasyWebRemoteControl remote;
remote.setAPConfig(IPAddress ip, IPAddress gateway, IPAddress subnet);
remote.setSTAStatic(IPAddress ip, IPAddress gateway, IPAddress subnet,
                  IPAddress dns1, IPAddress dns2 = IPAddress(0,0,0,0));
remote.setHostname(const char* name);
remote.beginAP(ssid, password);
remote.beginSTA(const char* ssid, const char* password, uint32_t connectTimeoutMs = 10000);
remote.beginDual(const char* apSsid, const char* apPassword,
               const char* staSsid, const char* staPassword,
               uint32_t connectTimeoutMs = 10000);

Callbacks
remote.onFront(func);
remote.onBack(func);
remote.onLeft(func);
remote.onRight(func);
remote.onStop(func);
void setVideoFrameProvider(func);

Slider
int PWM = remote.getPWM();
remote.setInitialPWM(128);
remote.showSlider(true);

Button Configuration
remote.addActionTimer("forward", 1000); // milliseconds
remote.setTaps("left", 2);
remote.setHold("right", 500); // ms
remote.setDelay("backward", 200); // ms

Video Configuration
void enableVideo(bool enable);
void setSnapshotFPS(uint8_t fps);
void pauseSnapshots(bool paused);
void setVideoFrameProvider(func);

Auto-Recovery Configuration
void enableAutoRecovery(bool enable);
void setAutoRecoveryTimings(int32_t reconnectWindowMs,
                                int32_t rebootAfterMs,
                                uint32_t checkIntervalMs = 5000,
                                uint32_t reconnectAfterMs = 30000,
                                uint32_t reconnectPeriodMs = 5000);



-Examples

The library comes with a BasicUsage example. Copy the folder to your Arduino examples directory or open it directly from the IDE:

BasicUsage: Demonstrates buttons, slider, and callbacks.




-License

MIT License. See LICENSE for details.



Notes for Users:

Creator: Colojoara Alexandru (BlueskyBV)

Contact email: alexcolojoara007@gmail.com

Version 3.0.0

Tested on ESP32 boards only.

Use recommended libraries.

Web interface runs at http://192.168.4.1/ by default in AP mode.

The password for the AP mode network your ESP32 hotspots must be at least 8 characters long.

Button IDs: Front Button --> "front"
	    Back Button --> "back"
	    Left Button --> "left"
    	    Right Button --> "right"
	    Middle Button (Stop Button) --> "stop"

Use, change and customize the library based on your project's needs!