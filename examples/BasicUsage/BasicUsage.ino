// EXAMPLE CODE FOR A MODIFIED SIMPLE REMOTE CONTROLED CAR USING ESP32 AND ESP32-CAMERA MODULE SHOWCASING SYNTAX, USAGE AND ALL OF THE BUILT-IN FUNCTIONALITIES

#include <EasyWebRemoteControl.h> // Include the library (MANDATORY)

// ---- Required camera headers for the official ESP32 camera driver (Easy_Web_Remote_Control supports any camera type. This type of camera is used for this example. The library's video capabilities are universal and work with any type of camera) ----
#include "esp_camera.h"
#include "img_converters.h"   // for frame2jpg() when we need to convert to JPEG


EasyWebRemoteControl controller; // Create your own instance that you'll use to call the methods inside the library. This way, by creating your own custom instance, users have a bit more freedom on how they call the library's methods (MANDATORY)

// Motor driver pins
const int IN1 = 1; // Left motor forward
const int IN2 = 2; // Left motor backward
const int IN3 = 3; // Right motor forward
const int IN4 = 4; // Right motor backward

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




//CAMERA USAGE
//Important example showcasing the CORRECT way to set up your camera in order to use it with our library
//PAY CLOSE ATTENTION! If you do not follow the suggestions, your video frame provider function might behave unexpectedly
//In this example, we show the most efficient, memory compact and fastest method to set up your camera and pair it nicely with the library's video capabilities
// -----------------------------------------------------------------------------
// Global state for safe buffer release (only used by the zero-copy path).
// We store the last frame buffer (fb) we handed to HTTP so we can give it back
// to the camera driver after the network stack finishes sending it.
// -----------------------------------------------------------------------------
static camera_fb_t* g_last_fb = nullptr;

// -----------------------------------------------------------------------------
// Release callback for the zero-copy path
//  - Called by your library AFTER the HTTP server finishes sending the snapshot
//  - We return the frame buffer (fb) to the camera driver for reuse
// -----------------------------------------------------------------------------
static void release_fb_zero_copy(const uint8_t* data_ptr) {
  // If we still own a frame and the pointer matches that frame's buffer,
  // hand it back to the camera driver so it can be recycled.
  if (g_last_fb && data_ptr == g_last_fb->buf) {
    esp_camera_fb_return(g_last_fb);
    g_last_fb = nullptr;
  }
}

// -----------------------------------------------------------------------------
// Release callback for the "copy/encode" path
//  - If we had to re-encode to JPEG into a heap buffer, simply free it.
// -----------------------------------------------------------------------------
static void release_heap_jpeg(const uint8_t* data_ptr) {
  // We allocated this JPEG buffer; just free it now that HTTP is done.
  free((void*)data_ptr);
}

// -----------------------------------------------------------------------------
// The frame provider function you pass to setVideoFrameProvider(...)
//  • Signature matches: bool (*)(EasyWebRemoteControl::VideoFrame& out)
//  • Returns 'true' if a frame is available right now, 'false' otherwise.
//  • Fills out.data / out.len / out.mime and sets out.release to a proper
//    cleanup function so memory/driver buffers are handled safely.
// -----------------------------------------------------------------------------
bool esp32camFrameProvider(EasyWebRemoteControl::VideoFrame& out) {
  // 1) Ask the camera driver for the latest frame.
  //    esp_camera_fb_get() returns a camera_fb_t* (or nullptr on failure).
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    // No frame available at this moment—tell the caller to try again later.
    return false;
  }

  // 2) FAST PATH (zero-copy): if the camera is configured to produce JPEG
  //    (PIXFORMAT_JPEG), the buffer is already a valid JPEG we can send.
  if (fb->format == PIXFORMAT_JPEG) {
    // Remember which fb we handed to HTTP so we can return it later in release().
    g_last_fb = fb;

    // Fill the outbound struct with the buffer details.
    out.data    = fb->buf;            // pointer to JPEG data owned by the camera driver
    out.len     = fb->len;            // exact byte length of the JPEG
    out.mime    = "image/jpeg";       // tell the browser the correct content type
    out.release = release_fb_zero_copy; // AFTER send, give fb back to the driver

    // Done—caller can immediately start sending this buffer.
    return true;
  }

  // 3) FALLBACK PATH (copy/encode): if the camera is NOT outputting JPEG
  //    (e.g., RGB565/YUV), we convert it to JPEG in RAM with frame2jpg().
  //    • This uses CPU and extra heap, but keeps API consistent.
  uint8_t* jpg_buf = nullptr;   // will hold the new JPEG
  size_t   jpg_len = 0;         // will hold its length
  const int jpeg_quality = 80;  // 10..90 typical; higher = bigger file / better quality

  // Convert the frame buffer to JPEG in memory.
  // frame2jpg(fb, quality, &out_buf, &out_len) returns true on success.
  bool ok = frame2jpg(fb, jpeg_quality, &jpg_buf, &jpg_len);

  // We must return the original frame buffer to the camera driver, since we’re done with it.
  esp_camera_fb_return(fb);

  // If conversion failed (e.g., not enough memory), bail out cleanly.
  if (!ok || !jpg_buf || jpg_len == 0) {
    if (jpg_buf) free(jpg_buf); // defensive: free partial buffer if any
    return false;
  }

  // Fill the outbound struct with our newly encoded JPEG buffer.
  out.data    = jpg_buf;              // pointer to our heap-allocated JPEG
  out.len     = jpg_len;              // its size
  out.mime    = "image/jpeg";         // content type
  out.release = release_heap_jpeg;    // AFTER send, free() this buffer

  // Success—caller can send this JPEG right away.
  return true;
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
  //In this example, clients connect to the wifi they set up the ESP32 to connect to then browse to their device's IP they set in the method. The one in the example might not work. You have to find your own device's IP to replace in the function and connect to after
  //If you choose not to use this method, the address you'll need to connect to after you are also connected to the wifi you wanted the ESP32 to connect to will be displayed in the serial monitor for you

  //controller.setHostName("Remote Car"); //Use this method to change the host name of your ESP32 when it connects to your wifi in the wifi list (OPTIONAL)

  //Use only 1 of these 3 methods to initialize web connection
  //controller.beginAP("RC_Car", "12345678"); //Use this method to begin the AP mode of the ESP32 and initialize the /ws server. Full freedom over WiFi name and password. Password must be at least 8 characters long (MANDATORY)
  //controller.beginSTA("TP-Link_77D0", "14638152"); //Use this method to begin the STA mode of the ESP32 and initialise the /ws server. Put as parameters the name and password of the wifi you want the ESP32 to connect to. You can also put a 3rd parameter which will be the number of miliseconds you want the trial of connectivity to expire if it can't connect to your wifi router. The default is 10000 ms. Skip this parameter if you want as it is only optional (MANDATORY)
  controller.beginDual("RC_Car", "12345678", "TP-Link_77D0", "14638152", 20000); //Use this method to begin the Dual mode of the ESP32 and initialise the /ws server. Input the AP and STA names and passwords. AP mode password must be at least 8 characters long. AP parameters first and STA parameters second. The connectivity expiration parameter is still in this method too and it is also optional so skip it if you don't want to change it (MANDATORY)

  // Attach commands (Mandatory if you want functionality on each button. If you don't want functionality on some button, it's optional, Assigning an onStop functionality is highly recommended!)
  //To attach functions, you pass as parameters the names of each function you wish to attach as pointers pointing to each function individually
  controller.onFront(forward); // Attach the "forward()" method you previously created to the up arrow button (MANDATORY/OPTIONAL)
  controller.onBack(backward); // Attach the "backward()" method you previously created to the down arrow button (MANDATORY/OPTIONAL)
  controller.onLeft(left); // Attach the "left()" method you previously created to the left arrow button (MANDATORY/OPTIONAL)
  controller.onRight(right); // Attach the "right()" method you previously created to the right arrow button (MANDATORY/OPTIONAL)
  controller.onStop(stopCar); // Attach the "stop()" method you previously created to the middle button. VERY IMPORTANT! YOUR CUSTOM METHOD YOU ATTACH USING onStop() SHOULD BE THE METHOD YOU WANT TO CALL TO STOP ALL MECHANISMS THAT YOU ARE CONTROLLING AS IT IS CALLED AUTOMATICALLY AT THE END OF ALL OTHER INPUTS TO AT LEAST THEORETICALLY STOP THE MECHANISMS NATURALLY (MANDATORY/OPTIONAL RECOMMENDED)

  // Other set-up methods the library features (should be called in setup)
  //Button function configuration
  controller.addActionTimer("front", 3000); // Method to be able to set for how long the attached function each button has runs for via button ID and a duration in miliseconds. addActionTimer([Button_ID], [Action Duration]); Use -1 as duration for infinite duration (requires manual stop). Use 0 for default run function while button is held functionality (already preset default button behaviour for all buttons) (OPTIONAL)
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
  //You use this to control the video functionalities of the library in an universal way using quick and efficient snapshots for your live camera feed
  controller.enableVideo(true);  //Method to enable/disable the video box on the website and and the video snapshooting functionalities (default is false) (OPTIONAL/MANDATORY if using video functionalities)
  controller.setSnapshotFPS(5); //Method to set the refreshrate (per second) of your personal connected camera's snapshots (default is 5) (OPTIONAL)
  controller.pauseSnapshots(false); //Method to pause/unpause the snapshots stream (OPTIONAL) (default is false)
  controller.setVideoFrameProvider(esp32camFrameProvider); //Method to register the function that supplies frames for the /snapshot HTTP route, the same way you set the functions for each button individualy. Without this, the endpoint returns 204 (No Content) (MANDATORY if using video functionalities/OPTIONAL) 
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