#ifndef EASYWEBREMOTECONTROL_H
#define EASYWEBREMOTECONTROL_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>
#include <vector>
#include <set>

class EasyWebRemoteControl {
public:
    // ---- Snapshot frame type & provider signature ----
    struct VideoFrame {
        const uint8_t* data = nullptr;          // buffer with frame data
        size_t         len = 0;                // buffer length
        const char* mime = "image/jpeg";     // "image/jpeg" or "image/png" etc.
        void (*release)(const uint8_t*) = nullptr; // optional deallocator for 'data'
    };
    using VideoProvider = bool (*)(VideoFrame& out); // return true if a frame is provided

    // ====================================================================
    // ============   fully customizable UI model  ==============
    // ====================================================================
    // A single button described entirely by data. Every visual attribute
    // has a default that reproduces the classic look, so an unconfigured
    // button is indistinguishable from the old hardcoded ones.
    struct UIButton {
        String id;                 // unique element id (also used as HTML id)
        String label = "";       // text/icon shown on the button (UTF-8 ok: arrows, emoji)
        String command = "";       // command sent on activation (defaults to id if empty)
        int    row = 0;        // which visual row this button belongs to

        // ---- styling (all optional; empty/<=0 means "use global/default") ----
        String bgColor = "";  // normal background, e.g. "#4CAF50"
        String pressedColor = "";  // background while pressed, e.g. "#388E3C"
        String hoverColor = "";  // background on mouse hover
        String textColor = "";  // label color, e.g. "white"
        int    width = 0;   // px (0 => global default)
        int    height = 0;   // px (0 => global default)
        int    fontSize = 0;   // px (0 => global default)
        String borderRadius = "";  // e.g. "12px" or "50%" for round
        bool   isStop = false; // true => uses the "stop everything" client handler
    };

    EasyWebRemoteControl();

    // ---- Network bring-up  ----
    void beginAP(const char* ssid, const char* password);
    void beginSTA(const char* ssid, const char* password, uint32_t connectTimeoutMs = 10000);
    void beginDual(const char* apSsid, const char* apPassword,
        const char* staSsid, const char* staPassword,
        uint32_t connectTimeoutMs = 10000);

    // Optional network configuration (call BEFORE beginAP/STA/Dual)
    void setAPConfig(IPAddress ip, IPAddress gateway, IPAddress subnet);
    void clearAPConfig(); // revert to DHCP-like auto IP for AP (default)
    void setSTAStatic(IPAddress ip, IPAddress gateway, IPAddress subnet,
        IPAddress dns1 = IPAddress(8, 8, 8, 8),
        IPAddress dns2 = IPAddress(1, 1, 1, 1));
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
    // ============  generic command callbacks  ================
    // ====================================================================
    // Register a handler for ANY command string (including custom buttons).
    // Works alongside the classic onFront/onBack/... handlers.
    void onCommand(const char* command, void (*func)());
    // Single catch-all handler; receives the raw command string.
    // Called for any command that has no specific handler registered.
    void onAnyCommand(void (*func)(const String& cmd));

    // ====================================================================
    // ============ full UI customization API  ================
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

    // ====================================================================
    // ============  security / protection layer  =============
    // ====================================================================
    // All security features default to OFF, so existing sketches are
    // unaffected. Enabling authentication turns on the full chain:
    // HTTP auth on every route + WebSocket token auth + rate limiting.

    // Enable HTTP authentication (protects "/", "/snapshot", and issues the
    // WebSocket token only to authenticated clients).
    void setAuthCredentials(const char* username, const char* password);
    void clearAuthCredentials();

    void setUseDigestAuth(bool useDigest);        // implicit true (Digest)
    void setAuthRealm(const char* realm);         // text shown in the browser login dialog
    void requireAuthForSnapshot(bool require);    // also protect the video endpoint (default true)

    // WebSocket token: auto-generated at startup with esp_random(); override if needed.
    void setAuthToken(const char* token);

    // Rate limiting: max commands accepted per client per second (0 = unlimited).
    void setRateLimit(uint16_t maxCommandsPerSec);

    // Command hardening: reject commands longer than maxLen bytes (anti-DoS).
    void setMaxCommandLength(uint16_t maxLen);

    // Brute-force protection: after maxAttempts failed HTTP logins from an IP,
    // lock that IP out for lockoutMs milliseconds (0 attempts = disabled).
    void setMaxAuthAttempts(uint8_t maxAttempts, uint32_t lockoutMs = 60000);

    // ---- Optional TLS (HTTPS/WSS) ----
    // Provide a PEM certificate + private key to serve over TLS. Requires an
    // ESP32 with PSRAM and a TLS-capable ESPAsyncWebServer build; when the
    // underlying stack lacks TLS support this is a no-op and a warning is
    // printed. Kept behind an explicit call so standard boards are unaffected.
    void setTLSCertificate(const char* certPem, const char* keyPem);

    // ====================================================================
    // ============ hardening + real encryption  =============
    // ====================================================================

    // ---- Session management ----
    // Rotate the WebSocket token on every page load and expire idle sessions.
    // A session that sees no command for sessionTimeoutMs is invalidated and
    // must re-authenticate. 0 = sessions never expire (previous behavior).
    void setSessionTimeout(uint32_t sessionTimeoutMs);
    void setRotateTokenPerLoad(bool rotate);   // new token each page load (default false)

    // ---- Security HTTP headers ----
    // Adds a hardened set of response headers (CSP, X-Frame-Options,
    // X-Content-Type-Options, Referrer-Policy, Permissions-Policy). Enabled
    // automatically when auth is on; call to force on/off explicitly.
    void setSecurityHeaders(bool enable);

    // ---- IP filtering ----
    // allowlist: if any IP is allowed, ONLY those IPs may connect.
    // blocklist: listed IPs are refused. allow takes precedence.
    void allowIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    void blockIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    void clearIPFilters();

    // ---- Security audit ----
    // Register a callback that fires on security-relevant events (auth failure,
    // lockout, blocked IP, rate-limit trip, bad WS token, decryption failure).
    enum SecurityEvent {
        SEC_AUTH_FAIL, SEC_AUTH_LOCKOUT, SEC_IP_BLOCKED,
        SEC_RATE_LIMIT, SEC_BAD_TOKEN, SEC_DECRYPT_FAIL, SEC_LOGIN_OK
    };
    void onSecurityEvent(void (*cb)(SecurityEvent ev, const String& detail));

    // ---- Application-layer encryption (AES-256-GCM) ----
    // Enables end-to-end encryption of WebSocket commands using ONLY audited
    // primitives: mbedTLS on the ESP32 and the browser's Web Crypto API.
    // A 256-bit key is derived from the passphrase via PBKDF2-HMAC-SHA256.
    //
    // IMPORTANT: the browser's Web Crypto (crypto.subtle) is available ONLY in
    // a "secure context" — HTTPS, localhost, or a tunnel. Over plain http://IP
    // it is undefined, so the page detects this and refuses to send commands
    // until served securely. This is a browser security rule, not a library
    // limitation: real confidentiality in a browser ultimately requires TLS or
    // a localhost/tunnel context. The passphrase itself is never transmitted.
    void setEncryptionKey(const char* passphrase);
    // Graceful degradation for encryption (default OFF = strict).
    // When the browser has no secure context, crypto.subtle is unavailable and
    // the page cannot encrypt. Strict mode (default) refuses to send anything —
    // never silently falling back to plaintext. With fallback enabled, the page
    // sends plaintext BUT displays a clearly visible warning badge, and the
    // device accepts both encrypted and plaintext commands. Use this only when
    // usability over plain HTTP matters more than guaranteed confidentiality
    // (for example a live demonstration on a local Access Point).
    void setEncryptionFallback(bool allowPlaintext);
    void clearEncryptionKey();

private:
    // Web server
    AsyncWebServer  server;
    AsyncWebSocket  ws;
    static EasyWebRemoteControl* instance;

    // Command callbacks (classic)
    void (*frontCallback)() = nullptr;
    void (*backCallback)() = nullptr;
    void (*leftCallback)() = nullptr;
    void (*rightCallback)() = nullptr;
    void (*stopCallback)() = nullptr;

    // generic command dispatch
    std::map<String, void(*)()> commandCallbacks;     // command -> handler
    void (*anyCommandCallback)(const String&) = nullptr;

    // PWM state
    int  currentPWM;
    bool sliderEnabled;
    // slider config
    int    sliderMin = 0;
    int    sliderMax = 255;
    String sliderLabel = "";
    int    sliderWidth = 220;

    // Behavior maps
    std::map<String, int> actionTimers;
    std::map<String, int> tapSettings;
    std::map<String, int> holdSettings;
    std::map<String, int> delaySettings;

    // Video state
    bool          videoEnabled = false;
    bool          videoPaused = false;
    uint8_t       snapshotFPS = 5;
    VideoProvider frameProvider = nullptr;

    // UI model
    std::vector<UIButton> uiButtons;       // empty => defaults injected lazily
    bool   defaultsInjected = false;
    // global styling
    String pageTitle = "Remote Control";
    String backgroundColor = "#f0f0f0";
    String fontFamily = "Arial,Helvetica,sans-serif";
    String defBtnColor = "#4CAF50";
    String defBtnPressed = "#388E3C";
    String defBtnHover = "#45a049";
    String defTextColor = "white";
    int    defBtnWidth = 100;
    int    defBtnHeight = 100;
    int    defFontSize = 36;
    String customCSS = "";
    String headerHTML = "";
    String footerHTML = "";

    // security state
    bool     authEnabled = false;
    String   authUser = "";
    String   authPass = "";
    bool     useDigestAuth = true;
    String   authRealm = "EasyWebRemoteControl";
    bool     protectSnapshot = true;
    String   wsToken = "";       // generated at startup or set explicitly
    bool     wsTokenSet = false;    // true if user provided a token
    uint16_t rateLimitPerSec = 0;        // 0 = disabled
    uint16_t maxCommandLength = 128;
    uint8_t  maxAuthAttempts = 0;        // 0 = brute-force protection disabled
    uint32_t authLockoutMs = 60000;
    bool     tlsEnabled = false;
    const char* tlsCert = nullptr;
    const char* tlsKey = nullptr;

    // runtime security tracking
    std::set<uint32_t> authedClients;                 // WS client ids that passed token auth
    struct RateInfo { uint32_t windowStartMs; uint16_t count; };
    std::map<uint32_t, RateInfo> rateMap;             // per-client command window
    struct AuthFailInfo { uint8_t failures; uint32_t lockedUntilMs; };
    std::map<uint32_t, AuthFailInfo> authFailMap;     // per-IP failed-login tracking

    // security helpers
    String genToken();
    bool   httpAuthOK(AsyncWebServerRequest* req);    // check auth + brute-force lockout
    bool   wsClientAuthed(uint32_t clientId);
    bool   rateLimited(uint32_t clientId);

    // hardening + encryption state
    uint32_t sessionTimeoutMs = 0;       // 0 = sessions never expire
    bool     rotateTokenPerLoad = false;
    bool     securityHeaders = false;   // auto-enabled with auth
    std::set<uint32_t>  allowedIPs;        // if non-empty, only these may connect
    std::set<uint32_t>  blockedIPs;
    void (*securityEventCb)(SecurityEvent, const String&) = nullptr;
    bool     encryptionEnabled = false;
    bool     encryptionFallback = false;   // allow plaintext when browser can't encrypt
    String   encryptionPass = "";      // passphrase (used to derive AES key)
    String   pbkdfSalt = "";      // random salt generated at startup

    // per-client last-activity timestamp for session expiry
    std::map<uint32_t, uint32_t> lastSeenMs;

    //  helpers
    bool   secureEquals(const String& a, const String& b);   // constant-time compare
    bool   ipFilterOK(AsyncWebServerRequest* req);
    bool   ipAllowed(uint32_t ipKey);
    void   fireSecurityEvent(SecurityEvent ev, const String& detail);
    void   addSecurityHeaders(AsyncWebServerResponse* resp);
    String tryDecryptCommand(const String& encB64);          // AES-256-GCM via mbedTLS
    uint32_t ipKeyOf(AsyncWebServerRequest* req);

    // Network config
    bool      apStaticSet = false;
    IPAddress apIP, apGW, apSN;

    bool      staStaticSet = false;
    IPAddress staIP, staGW, staSN, staDNS1, staDNS2;

    String    hostName;

    IPAddress lastApIPPrinted;
    IPAddress lastStaIPPrinted;

    // Internals
    void startServer();
    void handleCommand(String cmd);
    static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
        AwsEventType type, void* arg, uint8_t* data, size_t len);
    String buildHtmlPage();

    //  UI helpers
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
    bool          wdtInited = false;
    bool          autoRecoveryEnabled = true;
    int32_t       ar_reconnectWindowMs = -1;
    int32_t       ar_rebootAfterMs = 0;
    uint32_t      ar_checkIntervalMs = 5000;
    uint32_t      ar_reconnectAfterMs = 30000;
    uint32_t      ar_reconnectPeriodMs = 5000;

    unsigned long lastStaSeenConnectedMs = 0;
    unsigned long staDisconnectSinceMs = 0;
    unsigned long lastWifiCheckMs = 0;
    unsigned long lastReconnectAttemptMs = 0;

    void initWatchdogIfNeeded();
    void serviceWatchdog();
    void checkAndRecoverWiFi();
};

#endif