#ifndef EASYWEBREMOTECONTROL_H
#define EASYWEBREMOTECONTROL_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>
#include <vector>

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

    // ====================================================================
    // ============  NEW IN v4: fully customizable UI model  ==============
    // ====================================================================
    // A single button described entirely by data. Every visual attribute
    // has a default that reproduces the classic look, so an unconfigured
    // button is indistinguishable from the old hardcoded ones.
    struct UIButton {
        String id;                 // unique element id (also used as HTML id)
        String label   = "";       // text/icon shown on the button (UTF-8 ok: arrows, emoji)
        String command = "";       // command sent on activation (defaults to id if empty)
        int    row     = 0;        // which visual row this button belongs to

        // ---- styling (all optional; empty/<=0 means "use global/default") ----
        String bgColor      = "";  // normal background, e.g. "#4CAF50"
        String pressedColor = "";  // background while pressed, e.g. "#388E3C"
        String hoverColor   = "";  // background on mouse hover
        String textColor    = "";  // label color, e.g. "white"
        int    width        = 0;   // px (0 => global default)
        int    height       = 0;   // px (0 => global default)
        int    fontSize     = 0;   // px (0 => global default)
        String borderRadius = "";  // e.g. "12px" or "50%" for round
        bool   isStop       = false; // true => uses the "stop everything" client handler
    };

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

    // ---- Command callbacks (classic API — unchanged, fully backwards compatible) ----
    void onFront(void (*func)());
    void onBack(void (*func)());
    void onLeft(void (*func)());
    void onRight(void (*func)());
    void onStop(void (*func)());

    // ====================================================================
    // ============  NEW IN v4: generic command callbacks  ================
    // ====================================================================
    // Register a handler for ANY command string (including custom buttons).
    // Works alongside the classic onFront/onBack/... handlers.
    void onCommand(const char* command, void (*func)());
    // Single catch-all handler; receives the raw command string.
    // Called for any command that has no specific handler registered.
    void onAnyCommand(void (*func)(const String& cmd));

    // ====================================================================
    // ============  NEW IN v4: full UI customization API  ================
    // ====================================================================
    // --- Adding / managing buttons ---
    // Add a fully custom button. Returns nothing; chain setters by id afterwards.
    void addButton(const char* id, const char* label, const char* command = nullptr, int row = 0);
    // Convenience: add a button that maps to the classic directional commands.
    void clearButtons();                          // remove all buttons (incl. defaults)
    void removeButton(const char* id);            // remove one button by id

    // --- Per-button styling (operate on an existing button id) ---
    void setButtonLabel(const char* id, const char* label);
    void setButtonCommand(const char* id, const char* command);
    void setButtonRow(const char* id, int row);
    void setButtonColor(const char* id, const char* bgColor, const char* pressedColor = nullptr);
    void setButtonHoverColor(const char* id, const char* hoverColor);
    void setButtonTextColor(const char* id, const char* textColor);
    void setButtonSize(const char* id, int width, int height);
    void setButtonFontSize(const char* id, int fontSize);
    void setButtonBorderRadius(const char* id, const char* radius); // "12px", "50%", etc.
    // Mark a button as a "stop" button: its handler cancels ALL active timers
    // (the critical safety behavior). Auto-applied when id/command is "stop".
    void setStopButton(const char* id, bool isStop = true);

    // --- Global page styling ---
    void setPageTitle(const char* title);            // <h2> heading + <title>
    void setBackgroundColor(const char* color);      // page background
    void setFontFamily(const char* fontFamily);      // CSS font-family
    void setDefaultButtonColor(const char* bgColor, const char* pressedColor = nullptr);
    void setDefaultButtonSize(int width, int height);
    void setDefaultFontSize(int fontSize);
    void setDefaultTextColor(const char* color);
    // Inject extra raw CSS / HTML for power users (appended verbatim).
    void setCustomCSS(const char* css);
    void setHeaderHTML(const char* html);            // raw HTML above the controls
    void setFooterHTML(const char* html);            // raw HTML below the controls

    // ---- PWM slider API ----
    int  getPWM();
    void setInitialPWM(int val); // 0..255
    void showSlider(bool enable);
    // NEW IN v4: slider customization
    void setSliderRange(int minVal, int maxVal);     // default 0..255
    void setSliderLabel(const char* label);          // optional text near slider
    void setSliderWidth(int px);

    // ---- Per-button behavior config (unchanged) ----
    void addActionTimer(const char* buttonId, int durationMs); // -1 manual, 0 stop on release, >0 auto-stop ms
    void setTaps(const char* buttonId, int tapsRequired);      // 0 hold, >=1 tap count
    void setHold(const char* buttonId, int holdMs);            // 0 none, >0 ms
    void setDelay(const char* buttonId, int delayMs);          // 0 none, >0 ms

    // ---- Video / snapshot box controls (unchanged) ----
    void enableVideo(bool enable);
    void setSnapshotFPS(uint8_t fps);
    void pauseSnapshots(bool paused);
    void setVideoFrameProvider(VideoProvider p);

    // ---- Auto-recovery configuration (unchanged) ----
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

    // Command callbacks (classic)
    void (*frontCallback)() = nullptr;
    void (*backCallback)()  = nullptr;
    void (*leftCallback)()  = nullptr;
    void (*rightCallback)() = nullptr;
    void (*stopCallback)()  = nullptr;

    // NEW IN v4: generic command dispatch
    std::map<String, void(*)()> commandCallbacks;     // command -> handler
    void (*anyCommandCallback)(const String&) = nullptr;

    // PWM state
    int  currentPWM;
    bool sliderEnabled;
    // NEW IN v4: slider config
    int    sliderMin   = 0;
    int    sliderMax   = 255;
    String sliderLabel = "";
    int    sliderWidth = 220;

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

    // NEW IN v4: UI model
    std::vector<UIButton> uiButtons;       // empty => defaults injected lazily
    bool   defaultsInjected = false;
    // global styling
    String pageTitle        = "Remote Control";
    String backgroundColor  = "#f0f0f0";
    String fontFamily       = "Arial,Helvetica,sans-serif";
    String defBtnColor      = "#4CAF50";
    String defBtnPressed    = "#388E3C";
    String defBtnHover      = "#45a049";
    String defTextColor     = "white";
    int    defBtnWidth      = 100;
    int    defBtnHeight     = 100;
    int    defFontSize      = 36;
    String customCSS        = "";
    String headerHTML       = "";
    String footerHTML       = "";

    // Network config
    bool      apStaticSet  = false;
    IPAddress apIP, apGW, apSN;

    bool      staStaticSet = false;
    IPAddress staIP, staGW, staSN, staDNS1, staDNS2;

    String    hostName;

    IPAddress lastApIPPrinted;
    IPAddress lastStaIPPrinted;

    // Internals
    void startServer();
    void handleCommand(String cmd);
    static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len);
    String buildHtmlPage();

    // NEW IN v4: UI helpers
    void   ensureDefaultButtons();         // inject classic 5-button layout if empty
    UIButton* findButton(const char* id);  // locate a button by id (nullptr if absent)
    String jsEscape(const String& s);      // escape a string for safe JS embedding
    String htmlEscape(const String& s);    // escape a string for safe HTML embedding

    // Routes
    void addHttpRoutes();

    // Serial helpers
    void printUrlsIfChanged(bool forceOnce = false);
    static void printAddrLine(const char* label, const IPAddress& ip);

    // ---- Auto-recovery (state & config) ----
    bool          wdtInited              = false;
    bool          autoRecoveryEnabled    = true;
    int32_t       ar_reconnectWindowMs   = -1;
    int32_t       ar_rebootAfterMs       = 0;
    uint32_t      ar_checkIntervalMs     = 5000;
    uint32_t      ar_reconnectAfterMs    = 30000;
    uint32_t      ar_reconnectPeriodMs   = 5000;

    unsigned long lastStaSeenConnectedMs = 0;
    unsigned long staDisconnectSinceMs   = 0;
    unsigned long lastWifiCheckMs        = 0;
    unsigned long lastReconnectAttemptMs = 0;

    void initWatchdogIfNeeded();
    void serviceWatchdog();
    void checkAndRecoverWiFi();
};

#endif