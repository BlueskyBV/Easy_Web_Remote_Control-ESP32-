#ifndef EASYWEBREMOTECONTROL_H
#define EASYWEBREMOTECONTROL_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>

class EasyWebRemoteControl {
public:
    EasyWebRemoteControl();

    // ---------- Optional network config (call BEFORE beginAP/STA/Dual) ----------
    // Change the default AP network (default is 192.168.4.1 / 255.255.255.0)
    void setAPConfig(IPAddress ip, IPAddress gateway, IPAddress subnet);

    // Use a static IP for STA instead of DHCP (otherwise DHCP is used)
    bool setSTAStatic(IPAddress local, IPAddress gateway, IPAddress subnet,
                      IPAddress dns1 = IPAddress(8,8,8,8),
                      IPAddress dns2 = IPAddress(1,1,1,1));

    // Optional device hostname (mainly visible in STA/Dual mode)
    void setHostname(const char* name);

    // ---------- Network bring-up (no plain begin()) ----------
    void beginAP(const char* ssid, const char* password);
    void beginSTA(const char* ssid, const char* password, uint32_t connectTimeoutMs = 10000);
    void beginDual(const char* apSsid, const char* apPassword,
                   const char* staSsid, const char* staPassword,
                   uint32_t connectTimeoutMs = 10000);

    void update();

    // ---------- Callbacks (renamed) ----------
    void onFront(void (*func)());   // was onForward
    void onBack(void (*func)());    // was onBackward
    void onLeft(void (*func)());
    void onRight(void (*func)());
    void onStop(void (*func)());

    // ---------- PWM slider (renamed) ----------
    int  getPWM();                  // was getSpeed()
    void setInitialPWM(int val);    // was setInitialSpeed()
    void showSlider(bool enable);

    // ---------- Per-button behavior config ----------
    void addActionTimer(const char* buttonId, int durationMs); // -1 manual, 0 stop on release, >0 auto-stop ms
    void setTaps(const char* buttonId, int tapsRequired);      // 0 hold, >=1 tap count
    void setHold(const char* buttonId, int holdMs);            // 0 none, >0 ms
    void setDelay(const char* buttonId, int delayMs);          // 0 none, >0 ms

private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    static EasyWebRemoteControl* instance;

    // Internal callbacks
    void (*forwardCallback)();
    void (*backwardCallback)();
    void (*leftCallback)();
    void (*rightCallback)();
    void (*stopCallback)();

    int  currentSpeed;   // stores PWM 0..255
    bool sliderEnabled;

    std::map<String,int> actionTimers;
    std::map<String,int> tapSettings;
    std::map<String,int> holdSettings;
    std::map<String,int> delaySettings;

    // Optional network config flags/state
    bool      apCfgSet = false;
    IPAddress apIP, apGW, apSN;

    bool      staStaticSet = false;
    IPAddress staIP, staGW, staSN, staDNS1, staDNS2;

    String    hostName;  // optional hostname

    // Internals
    void startServer();
    void handleCommand(String cmd);
    static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len);
    String buildHtmlPage();
};

#endif
