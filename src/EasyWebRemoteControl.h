#ifndef EASYWEBREMOTECONTROL_H
#define EASYWEBREMOTECONTROL_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>

class EasyWebRemoteControl {
public:
    // ---- Snapshot frame type & provider signature ----
    struct VideoFrame {
        const uint8_t* data = nullptr;          // buffer with frame data
        size_t         len  = 0;                // buffer length
        const char*    mime = "image/jpeg";     // "image/jpeg" or "image/png" etc.
        void (*release)(const uint8_t*) = nullptr; // optional deallocator for 'data'
    };
    using VideoProvider = bool (*)(VideoFrame& out); // return true if a frame is provided

    EasyWebRemoteControl();

    // ---- Network bring-up (no plain begin() anymore) ----
    void beginAP(const char* ssid, const char* password);
    void beginSTA(const char* ssid, const char* password, uint32_t connectTimeoutMs = 10000);
    void beginDual(const char* apSsid, const char* apPassword,
                   const char* staSsid, const char* staPassword,
                   uint32_t connectTimeoutMs = 10000);

    // Optional network configuration (call BEFORE beginAP/STA/Dual)
    void setAPConfig(IPAddress ip, IPAddress gateway, IPAddress subnet);
    void clearAPConfig(); // revert to DHCP-like auto IP for AP (default)
    void setSTAStatic(IPAddress ip, IPAddress gateway, IPAddress subnet,
                      IPAddress dns1 = IPAddress(8,8,8,8),
                      IPAddress dns2 = IPAddress(1,1,1,1));
    void clearSTAStatic(); // revert to DHCP for STA (default)
    void setHostName(const char* host); // used for STA; optional

    void update(); // keep websockets tidy, auto-recovery checks, print URLs if IP changes

    // ---- Command callbacks ----
    void onFront(void (*func)());
    void onBack(void (*func)());
    void onLeft(void (*func)());
    void onRight(void (*func)());
    void onStop(void (*func)());

    // ---- PWM slider API ----
    int  getPWM();
    void setInitialPWM(int val); // 0..255
    void showSlider(bool enable);

    // ---- Per-button behavior config ----
    void addActionTimer(const char* buttonId, int durationMs); // -1 manual, 0 stop on release, >0 auto-stop ms
    void setTaps(const char* buttonId, int tapsRequired);      // 0 hold, >=1 tap count
    void setHold(const char* buttonId, int holdMs);            // 0 none, >0 ms
    void setDelay(const char* buttonId, int delayMs);          // 0 none, >0 ms

    // ---- Video / snapshot box controls ----
    void enableVideo(bool enable);                // show/hide snapshot box in UI
    void setSnapshotFPS(uint8_t fps);            // default 5; 0 disables auto-refresh
    void pauseSnapshots(bool paused);            // pause/resume refreshing in UI
    void setVideoFrameProvider(VideoProvider p); // set provider (optional)

    // ---- Auto-recovery configuration ----
    void enableAutoRecovery(bool enable);
    void setAutoRecoveryTimings(int32_t reconnectWindowMs,
                                int32_t rebootAfterMs,
                                uint32_t checkIntervalMs = 5000,
                                uint32_t reconnectAfterMs = 30000,
                                uint32_t reconnectPeriodMs = 5000);

private:
    // Web server
    AsyncWebServer  server;
    AsyncWebSocket  ws;
    static EasyWebRemoteControl* instance;

    // Command callbacks
    void (*frontCallback)() = nullptr;
    void (*backCallback)()  = nullptr;
    void (*leftCallback)()  = nullptr;
    void (*rightCallback)() = nullptr;
    void (*stopCallback)()  = nullptr;

    // PWM state
    int  currentPWM;
    bool sliderEnabled;

    // Behavior maps
    std::map<String,int> actionTimers;
    std::map<String,int> tapSettings;
    std::map<String,int> holdSettings;
    std::map<String,int> delaySettings;

    // Video state
    bool          videoEnabled     = false;
    bool          videoPaused      = false;
    uint8_t       snapshotFPS      = 5;
    VideoProvider frameProvider    = nullptr;

    // Network config
    bool      apStaticSet  = false;
    IPAddress apIP, apGW, apSN;

    bool      staStaticSet = false;
    IPAddress staIP, staGW, staSN, staDNS1, staDNS2;

    String    hostName; // optional; used for STA

    // Track last printed IPs to avoid serial spam
    IPAddress lastApIPPrinted;
    IPAddress lastStaIPPrinted;

    // Internals
    void startServer();
    void handleCommand(String cmd);
    static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len);
    String buildHtmlPage();

    // Routes
    void addHttpRoutes();

    // Serial helpers
    void printUrlsIfChanged(bool forceOnce = false);
    static void printAddrLine(const char* label, const IPAddress& ip);

    // ---- Auto-recovery (state & config) ----
    bool          wdtInited              = false;

    // user-configurable:
    bool          autoRecoveryEnabled    = true;      // default ON
    int32_t       ar_reconnectWindowMs   = -1;        // <0 forever; 0 never; >0 for N ms
    int32_t       ar_rebootAfterMs       = 0;         // 0 disabled; >0 reboot after ms disconnected
    uint32_t      ar_checkIntervalMs     = 5000;      // how often to check Wi-Fi
    uint32_t      ar_reconnectAfterMs    = 30000;     // start reconnect attempts after this long disconnected
    uint32_t      ar_reconnectPeriodMs   = 5000;      // minimum spacing between reconnect attempts

    // runtime tracking:
    unsigned long lastStaSeenConnectedMs = 0;
    unsigned long staDisconnectSinceMs   = 0;
    unsigned long lastWifiCheckMs        = 0;
    unsigned long lastReconnectAttemptMs = 0;

    // Auto-recovery helpers
    void initWatchdogIfNeeded();
    void serviceWatchdog();
    void checkAndRecoverWiFi();
};

#endif
