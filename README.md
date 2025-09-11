Easy_Web_Remote_Control by Colojoara Alexandru also known as BlueskyBV
Version 3.0.1

Control ESP32 systems via a web interface with the Easy_Web_Remote_Control library!
A C++ library suited for Arduino IDE to easily create a web-based remote control interface on ESP32 using WebSockets. Supports directional buttons, PWM output slider, connection auto-recovery, universal video capabilities using fast snapshots and configurable actions.



Table of Contents:
-Features

-Requirements

-Installation

-Usage

-API Reference

-Examples

-License

-Notes for Users

-Helpers and troubleshoots



-Features:
Web-based control of devices over Wi-Fi (supports AP mode, STA mode and Dual mode).

Directional buttons: Front, Back, Left, Right, Stop.

Optional PWM output slider (0–255).

Configurable action timers, taps, hold, and delay.

Fully asynchronous using WebSockets.

Configurable and universal video support for cameras using a fast snapshooting mechanism.

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
Installation for Arduino IDE.

Download the latest release of the library either from the Arduino Library Manager or GitHub (if from GitHub, add the library in the Arduino IDE manually).

Restart the Arduino IDE.



-Usage
Basic Setup:

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
Initialization:
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

Callbacks:
remote.onFront(void (*func)());
remote.onBack(void (*func)());
remote.onLeft(void (*func)());
remote.onRight(void (*func)());
remote.onStop(void (*func)());
remote.setVideoFrameProvider(VideoProvider p);  | Extra explanation: [using VideoProvider = bool (*) (VideoFrame& out);]

Slider:
int PWM = remote.getPWM();
remote.setInitialPWM(128);
remote.showSlider(true);

Button Configuration:
remote.addActionTimer("forward", 1000); // milliseconds
remote.setTaps("left", 2);
remote.setHold("right", 500); // ms
remote.setDelay("backward", 200); // ms

Video Configuration:
remote.enableVideo(bool enable);
remote.setSnapshotFPS(uint8_t fps);
remote.pauseSnapshots(bool paused);
remote.setVideoFrameProvider(VideoProvider p);  | Extra explanation: [using VideoProvider = bool (*) (VideoFrame& out);]

Auto-Recovery Configuration:
remote.enableAutoRecovery(bool enable);
remote.setAutoRecoveryTimings(int32_t reconnectWindowMs,
                                int32_t rebootAfterMs,
                                uint32_t checkIntervalMs = 5000,
                                uint32_t reconnectAfterMs = 30000,
                                uint32_t reconnectPeriodMs = 5000);



-Examples
The library comes with a BasicUsage example. Copy the folder to your Arduino examples directory or open it directly from the IDE:

BasicUsage: Demonstrates buttons, slider, and callbacks.



-License
MIT License. See LICENSE for details.



-Notes for Users:
Creator: Colojoara Alexandru (BlueskyBV)

Contact email: alexcolojoara007@gmail.com

Version 3.0.1

Tested on ESP32 boards only.

Use recommended libraries and their recommended versions.

Web interface runs at http://192.168.4.1/ by default in AP mode.

The password for the AP mode network that your ESP32 hotspots must be at least 8 characters long.

Button IDs: Front Button --> "front"
	    Back Button --> "back"
	    Left Button --> "left"
    	    Right Button --> "right"
	    Middle Button (Stop Button) --> "stop"

Use, change and customize the library based on your project's needs!






-More information, helpers and troubleshoots:
-VIDEO CAMERA API USE:
Video Snapshots API — Read This First (How to use it safely and fast)

Your ESP32 can serve “live” images by repeatedly fetching snapshots and showing them in the page.
This library is designed to make that as efficient as possible, but efficiency means you (the provider of the frame) must follow simple ownership rules about the memory you hand over.

This section explains how it works, what you must do, and how to avoid crashes or corrupted images and ensure 100% efficiency.


---

The mental model:

-The web UI hits GET /snapshot repeatedly (according to the FPS you configure).

-Each request, the library calls your function to provide one picture:


// Example to implement in your sketch:
bool myFrameProvider(EasyWebRemoteControl::VideoFrame& out);

If you return true, the library sends the bytes in out.data to the browser.

After the HTTP response finishes, the library calls your optional out.release function pointer so you can free or return the buffer.


The library itself never copies image data — it just passes through what you give it. That’s what makes it fast and memory-friendly.


---

The VideoFrame struct you need to fill:

struct VideoFrame {
    const uint8_t* data = nullptr;   // pointer to encoded image (e.g. JPEG)
    size_t         len  = 0;         // size in bytes
    const char*    mime = "image/jpeg"; // MIME type ("image/jpeg" or "image/png", etc.)
    void (release)(const uint8_t) = nullptr; // optional cleanup callback
};

data → must point to the raw image bytes.

len → exact size in bytes.

mime → MIME type string for the browser.

release → function pointer the library calls after sending.


---

Ownership rules (most important!):

1. You own the memory at data.
The library will not copy it.


2. That memory must remain valid until the HTTP response completes.
The library will call release(data) when it’s safe.


3. If no cleanup is required (e.g., global buffer, static array), set release = nullptr.


4. If cleanup is required (e.g., return a frame buffer to a driver), set release = someFunctionPointer that does the cleanup.


This gives you full control and efficiency — the library guarantees to call your cleanup only after it’s safe.


---

Correct, robust provider example (ESP32-CAM example):

#include "esp_camera.h"
#include <EasyWebRemoteControl.h>

// Static slot to remember the most recent frame buffer
static camera_fb_t* s_lastFb = nullptr;
static bool s_snapshotBusy   = false;

// Called by the library after it has finished sending the frame
static void release_camera_fb(const uint8_t*) {
    if (s_lastFb) {
        esp_camera_fb_return(s_lastFb);
        s_lastFb = nullptr;
    }
    s_snapshotBusy = false;
}

// Example provider function
bool myCamProvider(EasyWebRemoteControl::VideoFrame& out) {
    if (s_snapshotBusy) return false; // avoid overlap if two requests come quickly
    s_snapshotBusy = true;

    // 1) Get a frame from the camera
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || !fb->buf || fb->len == 0) {
        if (fb) esp_camera_fb_return(fb);
        s_snapshotBusy = false;
        return false;
    }

    // 2) Fill the VideoFrame
    s_lastFb  = fb;
    out.data  = fb->buf;
    out.len   = fb->len;
    out.mime  = "image/jpeg";
    out.release = &release_camera_fb; // safe cleanup after send

    return true; // success
}


Register the provider (in your setup()):

controller.setVideoFrameProvider(myCamProvider); //attach your video frame provider function
controller.enableVideo(true);       // show snapshot box in UI
controller.setSnapshotFPS(5);       // fetch ~5 times per second
controller.pauseSnapshots(false);   // set the pause of the snapshots to false to ensure smooth flow


---

Common usage patterns:

1. Camera driver (like ESP32-CAM for example. Works with any camera driver as the library's video capabilities are universal):

-esp_camera_fb_get() gives you a frame buffer.

-You must call esp_camera_fb_return() later → do this in your release function.


2. Static/global buffer:

-Example: pre-encoded JPEG stored in flash or a global RAM array.

-Set release = nullptr.

-Data stays valid permanently.


3. Dynamically allocated buffer:

-Allocate in provider with malloc() or new.

-Set release = &free (or your own cleanup function).

-Don’t free it yourself — the library calls release when it’s safe.


---

Do & Don’t:

✅ Do:

-Always set correct len and mime.

-Return false if no frame available (UI will try again).

-Use efficient sources (driver-managed buffers, global memory).


❌ Don’t:

-Don’t return a pointer to a local stack array (it’ll be invalid).

-Don’t free/return the buffer before the library calls your release.

-Don’t set wrong length — it will corrupt or crash the browser.


---

Controlling snapshot behavior:

In your sketch:

controller.enableVideo(true);    // show/hide <img> box
controller.setSnapshotFPS(8);    // 0 disables auto-refresh; otherwise N fps
controller.pauseSnapshots(false);// pause/resume refreshing

-The browser fetches /snapshot at the chosen rate.

-If your provider returns false, the library sends HTTP 204 (No Content).

-The UI just tries again at the next tick — no error shown.


---

Troubleshooting:

-Blank image / broken icon → wrong len, wrong mime, or you freed buffer too early.

-Crashes → buffer freed/returned before release was called.

-Lag/stutter → snapshot FPS too high; reduce setSnapshotFPS().

-Memory pressure → prefer driver-managed buffers (like ESP32-CAM fb), avoid extra mallocs.


---

TL;DR:

-You provide frames with setVideoFrameProvider().

-The library never copies — it just streams your bytes directly.

-You must keep the buffer valid until release is called.

-That’s why this design is fast, memory-efficient, and flexible.

-Use it smartly for fast powerful projects!


---


Follow the simple rules above and you’ll get reliable, fast, low-latency snapshots that are easy to pause, throttle, or process.



-ASYNC SERVER CRASH / TOO MANY CLIENTS:
Symptom: Random disconnects under load.

Fix: Keep snapshot FPS reasonable (e.g., 3–30). The UI polls; don’t set it over 30 unless you know your cam/CPU/network can handle it.



-CAMERA MEMORY ISSUES / CAMERA INSTABILITY
Symptom: Resets or Guru Meditation error during camera use.

Fixes:
Use ESP32 with PSRAM for higher JPEG resolutions; lower frame size if RAM is tight.

Ensure only one component owns and frees the frame. With esp32-cam use esp_camera_fb_get() + esp_camera_fb_return() from out.release only.



WEB PAGE LOADS BUT DOESN'T DO ANYTHING:
Symptom: No reactions to clicks.

Causes & Fixes:
WebSocket blocked by mixed content. Open the page over HTTP (not HTTPS) unless you also serve WSS.

Browser/extension/firewall blocking websockets—try another browser or network.

Didn’t call and use your callback setters (e.g., onFront(...)) correctly in setup().

Didn't call update() correctly in loop().



-WEB VIDEO BOX SHOWS BROKEN IMAGES / NEVER UPDATES:
Symptom: Broken image icon, or refresh spinner forever.

Causes & Fixes:
Your provider returned true but len/mime are wrong → fix both.

You freed/returned the buffer before send finished → move cleanup to out.release.

Provider returns false repeatedly → this is OK (204 No Content), but your camera may be failing. Log inside provider.



-RECONNECT LOGS WEIRDNESS:
Symptom: Logs say “Attempting auto-reconnect…” but no “Connected”.

Fix: That’s expected until your ESP32's Wi-Fi is back. On success the library prints “[STA] Reconnected successfully.” from checkAndRecoverWiFi(). If not, verify update() is being called regularly.



-BROWSER SHOWS STALE IMAGES:
Symptom: Same snapshot or image repeats.

Fixes: 
We send Cache-Control: no-store and a timestamp query in the library's .cpp; if you removed those, put them back.

Check the pauseSnapshots() function isn't being set to true.



-C++ GOTCHAS:
Symptom: Provider compiles but crashes.

Fix: Don’t return pointers to stack buffers. Use camera fb, a static/global buffer, or malloc + free via out.release.






Good luck!