#include "EasyWebRemoteControl.h"
#ifdef ESP32
#include "esp_task_wdt.h"
#include "esp_idf_version.h"
#include "esp_random.h"
// mbedTLS — bundled with the ESP32 core; used for real AES-256-GCM + PBKDF2.
#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#endif

// -------- static --------
EasyWebRemoteControl* EasyWebRemoteControl::instance = nullptr;

// -------- ctor --------
EasyWebRemoteControl::EasyWebRemoteControl()
    : server(80), ws("/ws"),
    currentPWM(0), sliderEnabled(true),
    lastApIPPrinted((uint32_t)0), lastStaIPPrinted((uint32_t)0)
{
    instance = this;
}

// -------- optional configuration (call BEFORE begin...) --------
void EasyWebRemoteControl::setAPConfig(IPAddress ip, IPAddress gateway, IPAddress subnet) {
    apStaticSet = true; apIP = ip; apGW = gateway; apSN = subnet;
}
void EasyWebRemoteControl::clearAPConfig() { apStaticSet = false; }

void EasyWebRemoteControl::setSTAStatic(IPAddress ip, IPAddress gateway, IPAddress subnet,
    IPAddress dns1, IPAddress dns2) {
    staStaticSet = true; staIP = ip; staGW = gateway; staSN = subnet; staDNS1 = dns1; staDNS2 = dns2;
}
void EasyWebRemoteControl::clearSTAStatic() { staStaticSet = false; }

void EasyWebRemoteControl::setHostName(const char* host) {
    if (host) hostName = host;
    else      hostName = "";
}

// ---- Auto-recovery configuration (public) ----
void EasyWebRemoteControl::enableAutoRecovery(bool enable) {
    autoRecoveryEnabled = enable;
}

void EasyWebRemoteControl::setAutoRecoveryTimings(int32_t reconnectWindowMs,
    int32_t rebootAfterMs,
    uint32_t checkIntervalMs,
    uint32_t reconnectAfterMs,
    uint32_t reconnectPeriodMs) {
    ar_reconnectWindowMs = reconnectWindowMs;
    ar_rebootAfterMs = rebootAfterMs;
    ar_checkIntervalMs = checkIntervalMs;
    ar_reconnectAfterMs = reconnectAfterMs;
    ar_reconnectPeriodMs = reconnectPeriodMs;
}

// -------- bring-up: AP / STA / Dual (UNCHANGED) --------
void EasyWebRemoteControl::beginAP(const char* ssid, const char* password) {
    WiFi.mode(WIFI_AP);
    if (apStaticSet) {
        if (!WiFi.softAPConfig(apIP, apGW, apSN)) {
            Serial.println("[AP] softAPConfig failed; using defaults.");
        }
    }
    WiFi.softAP(ssid, password);
    Serial.print("[AP] IP: ");
    Serial.println(WiFi.softAPIP());
    startServer();
    initWatchdogIfNeeded();
    printUrlsIfChanged(true);
}

void EasyWebRemoteControl::beginSTA(const char* ssid, const char* password, uint32_t connectTimeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(autoRecoveryEnabled);
    if (hostName.length()) {
        WiFi.setHostname(hostName.c_str());
    }
    if (staStaticSet) {
        if (!WiFi.config(staIP, staGW, staSN, staDNS1, staDNS2)) {
            Serial.println("[STA] WiFi.config failed; falling back to DHCP.");
        }
    }
    WiFi.begin(ssid, password);
    Serial.print("[STA] Connecting: "); Serial.print(ssid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < connectTimeoutMs) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[STA] IP: ");
        Serial.println(WiFi.localIP());
        lastStaSeenConnectedMs = millis();
        staDisconnectSinceMs = 0;
    }
    else {
        Serial.println("[STA] connect timeout; starting server anyway.");
    }
    startServer();
    initWatchdogIfNeeded();
    printUrlsIfChanged(true);
}

void EasyWebRemoteControl::beginDual(const char* apSsid, const char* apPassword,
    const char* staSsid, const char* staPassword,
    uint32_t connectTimeoutMs) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(autoRecoveryEnabled);
    if (apStaticSet) {
        if (!WiFi.softAPConfig(apIP, apGW, apSN)) {
            Serial.println("[AP] softAPConfig failed; using defaults.");
        }
    }
    WiFi.softAP(apSsid, apPassword);
    Serial.print("[AP] IP: ");
    Serial.println(WiFi.softAPIP());
    if (hostName.length()) {
        WiFi.setHostname(hostName.c_str());
    }
    if (staStaticSet) {
        if (!WiFi.config(staIP, staGW, staSN, staDNS1, staDNS2)) {
            Serial.println("[STA] WiFi.config failed; falling back to DHCP.");
        }
    }
    WiFi.begin(staSsid, staPassword);
    Serial.print("[STA] Connecting: "); Serial.print(staSsid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < connectTimeoutMs) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[STA] IP: ");
        Serial.println(WiFi.localIP());
        lastStaSeenConnectedMs = millis();
        staDisconnectSinceMs = 0;
    }
    else {
        Serial.println("[STA] connect timeout; AP remains active.");
    }
    startServer();
    initWatchdogIfNeeded();
    printUrlsIfChanged(true);
}

// -------- server wiring (generates WS token if auth is on) --------
void EasyWebRemoteControl::startServer() {
    // Generate a WebSocket auth token if authentication is enabled and the
    // user did not provide one explicitly.
    if (authEnabled && !wsTokenSet) {
        wsToken = genToken();
    }

    // generate a random salt for PBKDF2 key derivation (encryption).
    if (encryptionEnabled && pbkdfSalt.length() == 0) {
        pbkdfSalt = genToken();  // 32 hex chars = 16 bytes of salt
    }

    if (tlsEnabled) {
        // TLS is only available on a TLS-capable server build (needs PSRAM).
        // On the standard ESPAsyncWebServer this is a no-op; we warn and fall
        // back to plain HTTP so the device still works.
        Serial.println("[SEC] TLS requested but standard build serves plain HTTP; "
            "use a PSRAM board with a TLS-capable server for HTTPS/WSS.");
    }

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    addHttpRoutes();
    server.begin();
    Serial.println("[HTTP] Server started.");
    if (authEnabled)       Serial.println("[SEC] Authentication ENABLED.");
    if (encryptionEnabled) Serial.println("[SEC] Command encryption ENABLED (AES-256-GCM).");
}

void EasyWebRemoteControl::addHttpRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!instance) { request->send(500, "text/plain", "No instance"); return; }
        if (!instance->ipFilterOK(request)) return;   // 403 if IP not permitted
        if (!instance->httpAuthOK(request)) return;    // sends 401/403 as needed

        // rotate the WebSocket token on each page load if requested.
        if (instance->authEnabled && instance->rotateTokenPerLoad && !instance->wsTokenSet) {
            
            instance->wsToken = instance->genToken();
        }

        // Build a response so we can attach security headers.
        AsyncWebServerResponse* resp =
            request->beginResponse(200, "text/html", instance->buildHtmlPage());

        resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        instance->addSecurityHeaders(resp);
        request->send(resp);
        });
    server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!instance) { req->send(500, "text/plain", "No instance"); return; }
        if (!instance->ipFilterOK(req)) return;
        if (instance->authEnabled && instance->protectSnapshot) {
            if (!instance->httpAuthOK(req)) return;
        }
        if (!instance->videoEnabled || instance->videoPaused || !instance->frameProvider) {
            req->send(204);
            return;
        }
        VideoFrame f;
        bool ok = instance->frameProvider(f);
        if (!ok || !f.data || f.len == 0) {
            req->send(204);
            return;
        }
        AsyncWebServerResponse* resp = req->beginResponse_P(200, f.mime ? f.mime : "image/jpeg", f.data, f.len);
        resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        instance->addSecurityHeaders(resp);
        req->send(resp);
        if (f.release) f.release(f.data);
        });
}

// -------- update --------
void EasyWebRemoteControl::update() {
    ws.cleanupClients();
    serviceWatchdog();
    checkAndRecoverWiFi();
    printUrlsIfChanged(false);
    if (WiFi.getMode() & WIFI_MODE_STA) {
        static bool printed = false;
        if (WiFi.status() == WL_CONNECTED && !printed) {
            Serial.print("[STA] Connected, IP: ");
            Serial.println(WiFi.localIP());
            printed = true;
        }
    }
}

// -------- URL printing helpers  --------
void EasyWebRemoteControl::printUrlsIfChanged(bool forceOnce) {
    IPAddress ap = WiFi.softAPIP();
    IPAddress st = WiFi.localIP();
    bool apValid = (ap[0] != 0 || ap[1] != 0 || ap[2] != 0 || ap[3] != 0);
    bool stValid = (st[0] != 0 || st[1] != 0 || st[2] != 0 || st[3] != 0);
    if (forceOnce || (apValid && ap != lastApIPPrinted)) {
        lastApIPPrinted = ap;
        Serial.println("[URL] AP:");
        printAddrLine("  HTTP", ap);
        printAddrLine("  WS  ", ap);
    }
    if (forceOnce || (stValid && st != lastStaIPPrinted)) {
        lastStaIPPrinted = st;
        Serial.println("[URL] STA:");
        printAddrLine("  HTTP", st);
        printAddrLine("  WS  ", st);
        if (hostName.length()) {
            Serial.print("  Hostname: http://");
            Serial.print(hostName);
            Serial.println("/");
        }
    }
}

void EasyWebRemoteControl::printAddrLine(const char* label, const IPAddress& ip) {
    if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) return;
    Serial.print(label);
    Serial.print(": http://");
    Serial.print(ip);
    Serial.println("/");
}

// -------- Auto-recovery internals  --------
void EasyWebRemoteControl::initWatchdogIfNeeded() {
#ifdef ESP32
    if (wdtInited) return;
    esp_err_t err;
    err = esp_task_wdt_add(NULL);
    if (err == ESP_OK) {
        wdtInited = true;
    }
    else {
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
        esp_task_wdt_config_t cfg = {};
        cfg.timeout_ms = 8000;
        cfg.idle_core_mask = 0;
        cfg.trigger_panic = true;
        if (esp_task_wdt_init(&cfg) == ESP_OK) {
            if (esp_task_wdt_add(NULL) == ESP_OK) {
                wdtInited = true;
            }
        }
#else
        if (esp_task_wdt_init(8, true) == ESP_OK) {
            if (esp_task_wdt_add(NULL) == ESP_OK) {
                wdtInited = true;
            }
        }
#endif
    }
    if (wdtInited && WiFi.status() == WL_CONNECTED) {
        lastStaSeenConnectedMs = millis();
    }
#endif
}

void EasyWebRemoteControl::serviceWatchdog() {
#ifdef ESP32
    if (wdtInited) esp_task_wdt_reset();
#endif
}

void EasyWebRemoteControl::checkAndRecoverWiFi() {
    const unsigned long now = millis();
    if (now - lastWifiCheckMs < ar_checkIntervalMs) return;
    lastWifiCheckMs = now;
    if (!autoRecoveryEnabled) return;
    if (WiFi.getMode() & WIFI_MODE_STA) {
        wl_status_t s = WiFi.status();
        if (s == WL_CONNECTED) {
            if (staDisconnectSinceMs != 0 || lastStaSeenConnectedMs == 0) {
                Serial.println("[STA] Reconnected successfully.");
                printUrlsIfChanged(true);
            }
            lastStaSeenConnectedMs = now;
            staDisconnectSinceMs = 0;
            return;
        }
        if (staDisconnectSinceMs == 0) {
            staDisconnectSinceMs = now;
            Serial.println("[STA] Lost connection, trying to recover...");
        }
        const unsigned long disconnectedFor = now - staDisconnectSinceMs;
        if (ar_rebootAfterMs > 0 && disconnectedFor >= (unsigned long)ar_rebootAfterMs) {
            Serial.println("[STA] Disconnected too long; rebooting for recovery...");
            ESP.restart();
            return;
        }
        if (ar_reconnectWindowMs == 0) return;
        if (ar_reconnectWindowMs > 0 &&
            disconnectedFor > (unsigned long)ar_reconnectWindowMs) {
            return;
        }
        if (disconnectedFor < ar_reconnectAfterMs) return;
        if (now - lastReconnectAttemptMs >= ar_reconnectPeriodMs) {
            lastReconnectAttemptMs = now;
            Serial.println("[STA] Attempting auto-reconnect...");
            WiFi.reconnect();
        }
    }
}

// -------- public API: callbacks  --------
void EasyWebRemoteControl::onFront(void (*func)()) { frontCallback = func; }
void EasyWebRemoteControl::onBack(void (*func)()) { backCallback = func; }
void EasyWebRemoteControl::onLeft(void (*func)()) { leftCallback = func; }
void EasyWebRemoteControl::onRight(void (*func)()) { rightCallback = func; }
void EasyWebRemoteControl::onStop(void (*func)()) { stopCallback = func; }

// ====================================================================
// ============  generic command callbacks  ================
// ====================================================================
void EasyWebRemoteControl::onCommand(const char* command, void (*func)()) {
    if (!command) return;
    commandCallbacks[String(command)] = func;
}
void EasyWebRemoteControl::onAnyCommand(void (*func)(const String&)) {
    anyCommandCallback = func;
}

// -------- PWM API  --------
int  EasyWebRemoteControl::getPWM() { return currentPWM; }
void EasyWebRemoteControl::setInitialPWM(int val) {
    if (val < sliderMin) val = sliderMin;
    if (val > sliderMax) val = sliderMax;
    currentPWM = val;
}
void EasyWebRemoteControl::showSlider(bool enable) { sliderEnabled = enable; }
void EasyWebRemoteControl::setSliderRange(int minVal, int maxVal) {
    if (maxVal <= minVal) return;          // guard against invalid range
    sliderMin = minVal; sliderMax = maxVal;
    if (currentPWM < sliderMin) currentPWM = sliderMin;
    if (currentPWM > sliderMax) currentPWM = sliderMax;
}
void EasyWebRemoteControl::setSliderLabel(const char* label) { if (label) sliderLabel = label; }
void EasyWebRemoteControl::setSliderWidth(int px) { if (px > 0) sliderWidth = px; }

// -------- video controls  --------
void EasyWebRemoteControl::enableVideo(bool enable) { videoEnabled = enable; }
void EasyWebRemoteControl::setSnapshotFPS(uint8_t fps) { snapshotFPS = fps; }
void EasyWebRemoteControl::pauseSnapshots(bool paused) { videoPaused = paused; }
void EasyWebRemoteControl::setVideoFrameProvider(VideoProvider p) { frameProvider = p; }

// -------- per-button behavior  --------
void EasyWebRemoteControl::addActionTimer(const char* buttonId, int durationMs) {
    if (durationMs < -1) return;
    actionTimers[String(buttonId)] = durationMs;
}
void EasyWebRemoteControl::setTaps(const char* buttonId, int tapsRequired) {
    if (tapsRequired < 0) return;
    tapSettings[String(buttonId)] = tapsRequired;
}
void EasyWebRemoteControl::setHold(const char* buttonId, int holdMs) {
    if (holdMs < 0) return;
    holdSettings[String(buttonId)] = holdMs;
}
void EasyWebRemoteControl::setDelay(const char* buttonId, int delayMs) {
    if (delayMs < 0) return;
    delaySettings[String(buttonId)] = delayMs;
}

// ====================================================================
// ============  UI model management  =====================
// ====================================================================
EasyWebRemoteControl::UIButton* EasyWebRemoteControl::findButton(const char* id) {
    if (!id) return nullptr;
    String target(id);
    for (auto& b : uiButtons) {
        if (b.id == target) return &b;
    }
    return nullptr;
}

void EasyWebRemoteControl::addButton(const char* id, const char* label, const char* command, int row) {
    if (!id) return;
    // If a button with this id already exists, update it instead of duplicating.
    UIButton* existing = findButton(id);
    if (existing) {
        if (label)   existing->label = label;
        if (command) existing->command = command;
        existing->row = row;
        return;
    }
    UIButton b;
    b.id = id;
    b.label = label ? label : "";
    b.command = command ? command : id;   // default command == id
    b.row = row;
    // Auto-detect the stop button: if its id or command is "stop", it must use
    // the client-side "stop everything" handler (cancels all timers). This keeps
    // the critical safety behavior intact even when the user builds the UI manually.
    if (b.id == "stop" || b.command == "stop") b.isStop = true;
    uiButtons.push_back(b);
    defaultsInjected = true; // user is taking control; do not inject defaults later
}

void EasyWebRemoteControl::clearButtons() {
    uiButtons.clear();
    defaultsInjected = true; // explicit empty UI is a user choice
}

void EasyWebRemoteControl::removeButton(const char* id) {
    if (!id) return;
    String target(id);
    for (size_t i = 0; i < uiButtons.size(); ++i) {
        if (uiButtons[i].id == target) {
            uiButtons.erase(uiButtons.begin() + i);
            return;
        }
    }
}

// --- Per-button styling ---
void EasyWebRemoteControl::setButtonLabel(const char* id, const char* label) {
    UIButton* b = findButton(id); if (b && label) b->label = label;
}
void EasyWebRemoteControl::setButtonCommand(const char* id, const char* command) {
    UIButton* b = findButton(id); if (b && command) b->command = command;
}
void EasyWebRemoteControl::setButtonRow(const char* id, int row) {
    UIButton* b = findButton(id); if (b) b->row = row;
}
void EasyWebRemoteControl::setButtonColor(const char* id, const char* bgColor, const char* pressedColor) {
    UIButton* b = findButton(id);
    if (!b) return;
    if (bgColor)      b->bgColor = bgColor;
    if (pressedColor) b->pressedColor = pressedColor;
}
void EasyWebRemoteControl::setButtonHoverColor(const char* id, const char* hoverColor) {
    UIButton* b = findButton(id); if (b && hoverColor) b->hoverColor = hoverColor;
}
void EasyWebRemoteControl::setButtonTextColor(const char* id, const char* textColor) {
    UIButton* b = findButton(id); if (b && textColor) b->textColor = textColor;
}
void EasyWebRemoteControl::setButtonSize(const char* id, int width, int height) {
    UIButton* b = findButton(id);
    if (!b) return;
    if (width > 0) b->width = width;
    if (height > 0) b->height = height;
}
void EasyWebRemoteControl::setButtonFontSize(const char* id, int fontSize) {
    UIButton* b = findButton(id); if (b && fontSize > 0) b->fontSize = fontSize;
}
void EasyWebRemoteControl::setButtonBorderRadius(const char* id, const char* radius) {
    UIButton* b = findButton(id); if (b && radius) b->borderRadius = radius;
}
void EasyWebRemoteControl::setStopButton(const char* id, bool isStop) {
    UIButton* b = findButton(id); if (b) b->isStop = isStop;
}

// --- Global styling ---
void EasyWebRemoteControl::setPageTitle(const char* title) { if (title) pageTitle = title; }
void EasyWebRemoteControl::setBackgroundColor(const char* color) { if (color) backgroundColor = color; }
void EasyWebRemoteControl::setFontFamily(const char* family) { if (family) fontFamily = family; }
void EasyWebRemoteControl::setDefaultButtonColor(const char* bgColor, const char* pressedColor) {
    if (bgColor)      defBtnColor = bgColor;
    if (pressedColor) defBtnPressed = pressedColor;
}
void EasyWebRemoteControl::setDefaultButtonSize(int width, int height) {
    if (width > 0) defBtnWidth = width;
    if (height > 0) defBtnHeight = height;
}
void EasyWebRemoteControl::setDefaultFontSize(int fontSize) { if (fontSize > 0) defFontSize = fontSize; }
void EasyWebRemoteControl::setDefaultTextColor(const char* color) { if (color) defTextColor = color; }
void EasyWebRemoteControl::setCustomCSS(const char* css) { if (css) customCSS = css; }
void EasyWebRemoteControl::setHeaderHTML(const char* html) { if (html) headerHTML = html; }
void EasyWebRemoteControl::setFooterHTML(const char* html) { if (html) footerHTML = html; }

// ====================================================================
// ============ escaping helpers  ========================
// ====================================================================
// Escapes a string so it is safe to embed inside a double-quoted JS string.
String EasyWebRemoteControl::jsEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\'': out += "\\'";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '<':  out += "\\u003C"; break; // prevent </script> breakouts
        case '>':  out += "\\u003E"; break;
        default:   out += c;       break;
        }
    }
    return out;
}

// Escapes a string so it is safe to embed inside HTML text/attributes.
String EasyWebRemoteControl::htmlEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

// Injects the classic 5-button layout if the user never added any button.
// This guarantees byte-for-byte identical behavior to the legacy version
// for anyone who does not touch the new UI API.
void EasyWebRemoteControl::ensureDefaultButtons() {
    if (defaultsInjected || !uiButtons.empty()) return;

    UIButton front; front.id = "front"; front.label = "\xE2\x86\x91"; front.command = "front"; front.row = 0;
    UIButton left;  left.id = "left";  left.label = "\xE2\x86\x90"; left.command = "left";  left.row = 1;
    UIButton stop;  stop.id = "stop";  stop.label = "\xE2\x96\xA0"; stop.command = "stop";  stop.row = 1; stop.isStop = true;
    UIButton right; right.id = "right"; right.label = "\xE2\x86\x92"; right.command = "right"; right.row = 1;
    UIButton back;  back.id = "back";  back.label = "\xE2\x86\x93"; back.command = "back";  back.row = 2;

    uiButtons.push_back(front);
    uiButtons.push_back(left);
    uiButtons.push_back(stop);
    uiButtons.push_back(right);
    uiButtons.push_back(back);

    defaultsInjected = true;
}

// -------- command handling  --------
void EasyWebRemoteControl::handleCommand(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;
    // reject oversized commands (anti-DoS / anti buffer abuse).
    if (cmd.length() > maxCommandLength) return;

    if (cmd.startsWith("pwm:")) {
        int v = cmd.substring(4).toInt();
        if (v < sliderMin) v = sliderMin;
        if (v > sliderMax) v = sliderMax;
        currentPWM = v;
        return;
    }

    // (1) Classic fixed dispatch — preserved exactly, highest priority.
    if (cmd == "front" && frontCallback) { frontCallback(); return; }
    else if (cmd == "back" && backCallback) { backCallback();  return; }
    else if (cmd == "left" && leftCallback) { leftCallback();  return; }
    else if (cmd == "right" && rightCallback) { rightCallback(); return; }
    else if (cmd == "stop" && stopCallback) { stopCallback();  return; }

    // (2) per-command custom handlers registered via onCommand().
    auto it = commandCallbacks.find(cmd);
    if (it != commandCallbacks.end() && it->second) {
        it->second();
        return;
    }

    // (3)  catch-all handler registered via onAnyCommand().
    if (anyCommandCallback) {
        anyCommandCallback(cmd);
        return;
    }
    // Unknown command with no handler: silently ignored.
}

// -------- websocket (token auth + rate limiting) --------
void EasyWebRemoteControl::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
    AwsEventType type, void* arg, uint8_t* data, size_t len) {
    (void)server; (void)arg; // required by AsyncWebSocket signature, not used here
    if (!instance) return;

    const uint32_t cid = client ? client->id() : 0;

    if (type == WS_EVT_DISCONNECT) {
        // Clean up per-client security state to avoid leaks.
        instance->authedClients.erase(cid);
        instance->rateMap.erase(cid);
        instance->lastSeenMs.erase(cid);
        return;
    }

    if (type == WS_EVT_CONNECT) {
 
        instance->authedClients.erase(cid);
        instance->lastSeenMs.erase(cid);
        instance->rateMap.erase(cid);
        return;
    }

    if (type == WS_EVT_DATA) {
        String msg;
        msg.reserve(len);
        for (size_t i = 0; i < len; ++i) msg += (char)data[i];
        msg.trim();


        const size_t rawLimit = (size_t)instance->maxCommandLength * 4 + 64;
        if (msg.length() > rawLimit) return;

        const uint32_t now = millis();

        // ---- Authentication gate ----
        if (instance->authEnabled) {
            if (!instance->wsClientAuthed(cid)) {
                // Only accept an "auth:<token>" message until authenticated.
                if (msg.startsWith("auth:")) {
                    String tok = msg.substring(5);
                    //constant-time comparison (anti timing-attack).
                    if (instance->wsToken.length() && instance->secureEquals(tok, instance->wsToken)) {
                        instance->authedClients.insert(cid);
                        instance->lastSeenMs[cid] = now;
                        instance->fireSecurityEvent(SEC_LOGIN_OK, String("ws token ok (client #") + String(cid) + ")");
                        if (client) client->text("auth:ok");
                    }
                    else {
                        instance->fireSecurityEvent(SEC_BAD_TOKEN, String("ws token bad (client #") + String(cid) + ")");
                        if (client) client->text("auth:fail");
                        if (client) client->close();
                    }
                }
                else {

                    if (client) client->text("auth:required");
                }
                return;
            }

            // session expiry — invalidate idle sessions.
            if (instance->sessionTimeoutMs > 0) {
                uint32_t last = instance->lastSeenMs.count(cid) ? instance->lastSeenMs[cid] : now;
                if (now - last > instance->sessionTimeoutMs) {
                    instance->authedClients.erase(cid);
                    instance->lastSeenMs.erase(cid);
                    if (client) { client->text("auth:expired"); client->close(); }
                    return;
                }
            }
            instance->lastSeenMs[cid] = now;
        }

        // ---- Rate limiting ----
        if (instance->rateLimited(cid)) {
            instance->fireSecurityEvent(SEC_RATE_LIMIT, "rate limit");
            return;
        }

        // ---- decryption of encrypted commands ----
        // Encrypted commands arrive as "enc:<base64>". In strict mode (default)
        // plaintext control commands are rejected so nothing leaks in the clear.
        // With setEncryptionFallback(true), plaintext is accepted as a documented
        // degradation for browsers without a secure context (the page shows a
        // visible warning in that case).
        if (instance->encryptionEnabled) {
            if (msg.startsWith("enc:")) {
                String plain = instance->tryDecryptCommand(msg.substring(4));
                if (plain.length() == 0) return;   // decryption failed (event already fired)
                instance->handleCommand(plain);
                return;
            }
            if (!instance->encryptionFallback) {
                // Strict: never accept plaintext while encryption is on.
                return;
            }
            // Fallback: fall through and accept the plaintext command.
        }

        instance->handleCommand(msg);
    }
}
// ====================================================================
// ============  security implementation  =================
// ====================================================================

// ---- public configuration ----
void EasyWebRemoteControl::setAuthCredentials(const char* username, const char* password) {
    if (!username || !password) return;
    authUser = username;
    authPass = password;
    authEnabled = true;
}
void EasyWebRemoteControl::clearAuthCredentials() {
    authEnabled = false; authUser = ""; authPass = "";
}
void EasyWebRemoteControl::setUseDigestAuth(bool useDigest) { useDigestAuth = useDigest; }
void EasyWebRemoteControl::setAuthRealm(const char* realm) { if (realm) authRealm = realm; }
void EasyWebRemoteControl::requireAuthForSnapshot(bool require) { protectSnapshot = require; }
void EasyWebRemoteControl::setAuthToken(const char* token) {
    if (!token) return;
    wsToken = token; wsTokenSet = true;
}
void EasyWebRemoteControl::setRateLimit(uint16_t maxCommandsPerSec) { rateLimitPerSec = maxCommandsPerSec; }
void EasyWebRemoteControl::setMaxCommandLength(uint16_t maxLen) { if (maxLen > 0) maxCommandLength = maxLen; }
void EasyWebRemoteControl::setMaxAuthAttempts(uint8_t maxAttempts, uint32_t lockoutMs) {
    maxAuthAttempts = maxAttempts; authLockoutMs = lockoutMs;
}
void EasyWebRemoteControl::setTLSCertificate(const char* certPem, const char* keyPem) {
    if (!certPem || !keyPem) return;
    tlsCert = certPem; tlsKey = keyPem; tlsEnabled = true;
}

// ---- token generation (cryptographically-seeded on ESP32) ----
String EasyWebRemoteControl::genToken() {
    // 32 hex chars = 128 bits of entropy from the hardware RNG.
    const char* hex = "0123456789abcdef";
    String t;
    t.reserve(32);
    for (int i = 0; i < 16; ++i) {
#ifdef ESP32
        uint8_t b = (uint8_t)(esp_random() & 0xFF);
#else
        uint8_t b = (uint8_t)(rand() & 0xFF);
#endif
        t += hex[(b >> 4) & 0x0F];
        t += hex[b & 0x0F];
    }
    return t;
}

// ---- HTTP auth check with brute-force lockout ----
bool EasyWebRemoteControl::httpAuthOK(AsyncWebServerRequest* req) {
    if (!authEnabled) return true;

    // Identify the client IP for brute-force tracking.
    uint32_t ipKey = 0;
    IPAddress rip = req->client()->remoteIP();
    ipKey = ((uint32_t)rip[0]) | ((uint32_t)rip[1] << 8) |
        ((uint32_t)rip[2] << 16) | ((uint32_t)rip[3] << 24);

    const uint32_t now = millis();

    // Enforce lockout if brute-force protection is enabled.
    if (maxAuthAttempts > 0) {
        auto it = authFailMap.find(ipKey);
        if (it != authFailMap.end() && it->second.lockedUntilMs != 0) {
            if (now < it->second.lockedUntilMs) {
                fireSecurityEvent(SEC_AUTH_LOCKOUT, "ip locked out");
                req->send(429, "text/plain", "Too many attempts. Try again later.");
                return false;
            }
            else {
                // Lockout expired; reset.
                it->second.failures = 0;
                it->second.lockedUntilMs = 0;
            }
        }
    }

    // Validate credentials (Basic or Digest handled by the server library).
    bool ok = req->authenticate(authUser.c_str(), authPass.c_str());
    if (ok) {
        if (maxAuthAttempts > 0) authFailMap.erase(ipKey);
        fireSecurityEvent(SEC_LOGIN_OK, "http login ok");
        return true;
    }

    // Failed: record attempt and possibly lock out.
    fireSecurityEvent(SEC_AUTH_FAIL, "http login fail");
    if (maxAuthAttempts > 0) {
        
        if (authFailMap.size() >= 64 && authFailMap.find(ipKey) == authFailMap.end()) {
            authFailMap.clear();
        }
        AuthFailInfo& info = authFailMap[ipKey];
        info.failures++;
        if (info.failures >= maxAuthAttempts) {
            info.lockedUntilMs = now + authLockoutMs;
            fireSecurityEvent(SEC_AUTH_LOCKOUT, "ip locked out");
            Serial.println("[SEC] IP locked out after repeated auth failures.");
        }
    }
    // Ask the browser to authenticate (shows the login dialog).
    req->requestAuthentication(authRealm.c_str());
    return false;
}

bool EasyWebRemoteControl::wsClientAuthed(uint32_t clientId) {
    return authedClients.find(clientId) != authedClients.end();
}

// ---- per-client rate limiting (sliding 1-second window) ----
bool EasyWebRemoteControl::rateLimited(uint32_t clientId) {
    if (rateLimitPerSec == 0) return false; // disabled
    const uint32_t now = millis();
    RateInfo& r = rateMap[clientId];
    if (now - r.windowStartMs >= 1000) {
        r.windowStartMs = now;
        r.count = 0;
    }
    r.count++;
    return (r.count > rateLimitPerSec);
}

// ====================================================================
// ============  hardening + encryption impl  =============
// ====================================================================

// ---- public configuration ----
void EasyWebRemoteControl::setSessionTimeout(uint32_t ms) { sessionTimeoutMs = ms; }
void EasyWebRemoteControl::setRotateTokenPerLoad(bool rotate) { rotateTokenPerLoad = rotate; }
void EasyWebRemoteControl::setSecurityHeaders(bool enable) { securityHeaders = enable; }

void EasyWebRemoteControl::allowIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint32_t k = ((uint32_t)a) | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
    allowedIPs.insert(k);
}
void EasyWebRemoteControl::blockIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint32_t k = ((uint32_t)a) | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
    blockedIPs.insert(k);
}
void EasyWebRemoteControl::clearIPFilters() { allowedIPs.clear(); blockedIPs.clear(); }

void EasyWebRemoteControl::onSecurityEvent(void (*cb)(SecurityEvent, const String&)) {
    securityEventCb = cb;
}

void EasyWebRemoteControl::setEncryptionKey(const char* passphrase) {
    if (!passphrase) return;
    encryptionPass = passphrase;
    encryptionEnabled = true;
}
void EasyWebRemoteControl::setEncryptionFallback(bool allowPlaintext) {
    encryptionFallback = allowPlaintext;
}
void EasyWebRemoteControl::clearEncryptionKey() {
    encryptionEnabled = false; encryptionPass = "";
}

// ---- constant-time string comparison (anti timing-attack) ----
// Compares in time proportional to the longer input, independent of where the
// first difference occurs, so an attacker cannot learn the secret byte by byte.
bool EasyWebRemoteControl::secureEquals(const String& a, const String& b) {
    const size_t la = a.length(), lb = b.length();
    const size_t n = (la > lb) ? la : lb;
    uint8_t diff = (uint8_t)(la ^ lb);   // length mismatch also contributes
    for (size_t i = 0; i < n; ++i) {
        uint8_t ca = (i < la) ? (uint8_t)a[i] : 0;
        uint8_t cb = (i < lb) ? (uint8_t)b[i] : 0;
        diff |= (uint8_t)(ca ^ cb);
    }
    return diff == 0;
}

uint32_t EasyWebRemoteControl::ipKeyOf(AsyncWebServerRequest* req) {
    IPAddress rip = req->client()->remoteIP();
    return ((uint32_t)rip[0]) | ((uint32_t)rip[1] << 8) |
        ((uint32_t)rip[2] << 16) | ((uint32_t)rip[3] << 24);
}

bool EasyWebRemoteControl::ipAllowed(uint32_t ipKey) {
    // Blocklist always wins.
    if (blockedIPs.find(ipKey) != blockedIPs.end()) return false;
    // If an allowlist exists, the IP must be in it.
    if (!allowedIPs.empty() && allowedIPs.find(ipKey) == allowedIPs.end()) return false;
    return true;
}

bool EasyWebRemoteControl::ipFilterOK(AsyncWebServerRequest* req) {
    if (allowedIPs.empty() && blockedIPs.empty()) return true;
    uint32_t k = ipKeyOf(req);
    if (!ipAllowed(k)) {
        fireSecurityEvent(SEC_IP_BLOCKED, "IP not permitted");
        req->send(403, "text/plain", "Forbidden");
        return false;
    }
    return true;
}

void EasyWebRemoteControl::fireSecurityEvent(SecurityEvent ev, const String& detail) {
    if (securityEventCb) securityEventCb(ev, detail);
}

void EasyWebRemoteControl::addSecurityHeaders(AsyncWebServerResponse* resp) {
    if (!resp) return;
    if (!(securityHeaders || authEnabled)) return;
    // A conservative, self-contained policy. 'unsafe-inline' is required because
    // the UI ships inline CSS/JS generated on-device; everything else is locked.
    resp->addHeader("Content-Security-Policy",
        "default-src 'self'; script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
        "connect-src 'self' ws: wss:; frame-ancestors 'none'; base-uri 'none'");
    resp->addHeader("X-Frame-Options", "DENY");
    resp->addHeader("X-Content-Type-Options", "nosniff");
    resp->addHeader("Referrer-Policy", "no-referrer");
    resp->addHeader("Permissions-Policy", "geolocation=(), microphone=(), camera=()");
    if (tlsEnabled) {
        resp->addHeader("Strict-Transport-Security", "max-age=31536000");
    }
}

// ---- AES-256-GCM decryption via mbedTLS ----
// Input: base64 of [12-byte IV][ciphertext][16-byte tag].
// Key: PBKDF2-HMAC-SHA256(passphrase, salt, 100000, 32 bytes).
// Returns the decrypted command, or an empty string on any failure.
String EasyWebRemoteControl::tryDecryptCommand(const String& encB64) {
#ifdef ESP32
    if (!encryptionEnabled || encryptionPass.length() == 0) return String("");

    // 1) base64-decode the payload.
    const size_t inLen = encB64.length();
    size_t rawLen = 0;
    // First call to get required size.
    unsigned char* raw = (unsigned char*)malloc(inLen); // decoded <= input length
    if (!raw) return String("");
    int rc = mbedtls_base64_decode(raw, inLen, &rawLen,
        (const unsigned char*)encB64.c_str(), inLen);
    if (rc != 0 || rawLen < (12 + 16 + 1)) { free(raw); fireSecurityEvent(SEC_DECRYPT_FAIL, "b64"); return String(""); }

    const unsigned char* iv = raw;                 // 12 bytes
    const unsigned char* ct = raw + 12;            // ciphertext
    const size_t ctLen = rawLen - 12 - 16;    // minus IV and tag
    const unsigned char* tag = raw + 12 + ctLen;    // 16 bytes

    // 2) derive the 256-bit key with PBKDF2-HMAC-SHA256.
    unsigned char key[32];
    const mbedtls_md_info_t* mdinfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdinfo) { free(raw); return String(""); }
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x03000000)
    // mbedTLS 3.x (ESP-IDF v5+)
    rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        (const unsigned char*)encryptionPass.c_str(), encryptionPass.length(),
        (const unsigned char*)pbkdfSalt.c_str(), pbkdfSalt.length(),
        100000, sizeof(key), key);
#else
    // mbedTLS 2.x (ESP-IDF v4)
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    rc = mbedtls_md_setup(&md_ctx, mdinfo, 1);   // 1 = HMAC
    if (rc == 0) {
        rc = mbedtls_pkcs5_pbkdf2_hmac(&md_ctx,
            (const unsigned char*)encryptionPass.c_str(), encryptionPass.length(),
            (const unsigned char*)pbkdfSalt.c_str(), pbkdfSalt.length(),
            100000, sizeof(key), key);
    }
    mbedtls_md_free(&md_ctx);
#endif
    if (rc != 0) { free(raw); fireSecurityEvent(SEC_DECRYPT_FAIL, "pbkdf2"); return String(""); }

    // 3) AES-256-GCM authenticated decryption.
    unsigned char* out = (unsigned char*)malloc(ctLen + 1);
    if (!out) { free(raw); return String(""); }
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen,
            iv, 12, nullptr, 0, tag, 16, ct, out);
    }
    mbedtls_gcm_free(&gcm);
    free(raw);

    if (rc != 0) { free(out); fireSecurityEvent(SEC_DECRYPT_FAIL, "gcm"); return String(""); }
    out[ctLen] = 0;
    String result = String((const char*)out);
    free(out);
    return result;
#else
    (void)encB64;
    return String("");
#endif
}

// ====================================================================
// ============  data-driven page builder  ================
// ====================================================================
// This rewrite generates the UI from the uiButtons vector instead of
// hardcoded HTML. If the user never configured anything, ensureDefaultButtons()
// injects the classic 5-button layout, so the output is functionally identical
// to the legacy version. The embedded JavaScript behavior engine (taps / hold /
// delay / action timers / stop-all) is preserved verbatim — only the button
// list it operates on is now generated dynamically.

String EasyWebRemoteControl::buildHtmlPage() {
    // Make sure we have something to render (classic defaults if untouched).
    ensureDefaultButtons();

    // ---- Serialize behavior configuration maps to JS objects ----
    // (Bug fixed vs legacy: each map now uses its OWN length, not timersJS's.)
    String timersJS = "const actionTimers={";
    for (auto& kv : actionTimers) { timersJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!actionTimers.empty()) timersJS.remove(timersJS.length() - 1);
    timersJS += "};";

    String tapsJS = "const tapConfig={";
    for (auto& kv : tapSettings) { tapsJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!tapSettings.empty()) tapsJS.remove(tapsJS.length() - 1);
    tapsJS += "};";

    String holdJS = "const holdConfig={";
    for (auto& kv : holdSettings) { holdJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!holdSettings.empty()) holdJS.remove(holdJS.length() - 1);
    holdJS += "};";

    String delayJS = "const delayConfig={";
    for (auto& kv : delaySettings) { delayJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!delaySettings.empty()) delayJS.remove(delayJS.length() - 1);
    delayJS += "};";

    // ---- Build a JS map: command-per-button-id, plus a list of interactive
    //      (non-stop) buttons and the id(s) flagged as "stop". ----
    String btnCmdJS = "const btnCommands={";
    String interactiveListJS = "const interactiveButtons=[";
    String stopListJS = "const stopButtons=[";
    bool firstCmd = true, firstInteractive = true, firstStop = true;
    for (auto& b : uiButtons) {
        String cmd = b.command.length() ? b.command : b.id;
        if (!firstCmd) btnCmdJS += ",";
        btnCmdJS += "\"" + jsEscape(b.id) + "\":\"" + jsEscape(cmd) + "\"";
        firstCmd = false;

        if (b.isStop) {
            if (!firstStop) stopListJS += ",";
            stopListJS += "\"" + jsEscape(b.id) + "\"";
            firstStop = false;
        }
        else {
            if (!firstInteractive) interactiveListJS += ",";
            interactiveListJS += "\"" + jsEscape(b.id) + "\"";
            firstInteractive = false;
        }
    }
    btnCmdJS += "};";
    interactiveListJS += "];";
    stopListJS += "];";

    // ---- Per-button CSS overrides ----
    // Generate a rule for each button that has at least one custom attribute.
    String perBtnCSS = "";
    for (auto& b : uiButtons) {
        String rule = "";
        if (b.bgColor.length())      rule += "background:" + b.bgColor + ";";
        if (b.textColor.length())    rule += "color:" + b.textColor + ";";
        if (b.width > 0)            rule += "width:" + String(b.width) + "px;";
        if (b.height > 0)            rule += "height:" + String(b.height) + "px;";
        if (b.fontSize > 0)          rule += "font-size:" + String(b.fontSize) + "px;";
        if (b.borderRadius.length()) rule += "border-radius:" + b.borderRadius + ";";
        if (rule.length()) {
            // Use attribute selector on data-id to avoid clashes with HTML id rules.
            perBtnCSS += "button[data-id=\"" + b.id + "\"]{" + rule + "}";
        }
        if (b.pressedColor.length()) {
            perBtnCSS += "button[data-id=\"" + b.id + "\"].pressed{background:" + b.pressedColor + ";}";
        }
        if (b.hoverColor.length()) {
            perBtnCSS += "button[data-id=\"" + b.id + "\"]:hover{background:" + b.hoverColor + ";}";
        }
    }

    // ---- Group buttons by row and emit the HTML ----
    // Find max row to iterate in order.
    int maxRow = 0;
    for (auto& b : uiButtons) if (b.row > maxRow) maxRow = b.row;

    String controlsHTML = "";
    for (int r = 0; r <= maxRow; ++r) {
        String rowHTML = "";
        bool any = false;
        for (auto& b : uiButtons) {
            if (b.row != r) continue;
            any = true;
            // data-id carries the (unescaped-for-HTML-attr) id; htmlEscape keeps it safe.
            rowHTML += "<button data-id=\"" + htmlEscape(b.id) + "\">"
                + htmlEscape(b.label) + "</button>";
        }
        if (any) {
            controlsHTML += "<div class=\"row\">" + rowHTML + "</div>";
        }
    }

    // ---- Slider block ----
    String sliderUI = "";
    if (sliderEnabled) {
        String labelHTML = sliderLabel.length()
            ? "<span class=\"sliderLabel\">" + htmlEscape(sliderLabel) + "</span>" : "";
        sliderUI =
            "<div class=\"row\">"
            + labelHTML +
            "<input type=\"range\" id=\"pwmSlider\" min=\"" + String(sliderMin) +
            "\" max=\"" + String(sliderMax) + "\" value=\"" + String(currentPWM) + "\">"
            "<span id=\"pwmValue\">" + String(currentPWM) + "</span>"
            "</div>";
    }

    // ---- Video block ----
    String videoUI = "";
    if (videoEnabled) {
        videoUI =
            "<div class=\"row\" id=\"videoRow\">"
            "<img id=\"snapImg\" alt=\"\" style=\"max-width:320px;max-height:240px;border-radius:8px;box-shadow:0 1px 4px rgba(0,0,0,.2)\"/>"
            "</div>";
    }

    // ---- Assemble full page ----
    String page =
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>" + htmlEscape(pageTitle) + "</title>"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>"
        "body{text-align:center;font-family:" + fontFamily + ";background:" + backgroundColor + ";margin:0;padding:12px;-webkit-touch-callout:none;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;}"
        "h2{margin:18px 0;font-size:20px;}"
        ".row{margin:14px 0;display:flex;justify-content:center;align-items:center;gap:12px;-webkit-tap-highlight-color:transparent;flex-wrap:wrap;}"
        "button{width:" + String(defBtnWidth) + "px;height:" + String(defBtnHeight) + "px;font-size:" + String(defFontSize) + "px;margin:0;border-radius:12px;cursor:pointer;background:" + defBtnColor + ";color:" + defTextColor + ";border:none;transition:0.18s;}"
        "button.pressed{background:" + defBtnPressed + ";transform:translateY(1px);}"
        "button:hover{background-color:" + defBtnHover + ";}"
        "input[type=range]{width:" + String(sliderWidth) + "px;}#pwmValue{display:inline-block;width:44px;text-align:center;font-size:18px;margin-left:10px;}"
        ".sliderLabel{font-size:16px;margin-right:8px;}"
        "@media(max-width:480px){button{width:80px;height:80px;font-size:28px;}input[type=range]{width:180px;}}"
        + perBtnCSS
        + customCSS +
        "</style></head><body>"
        + headerHTML +
        "<h2>" + htmlEscape(pageTitle) + "</h2><div id=\"controls\">"
        + controlsHTML + sliderUI + videoUI +
        "</div>"
        + footerHTML +
        "<script>" + timersJS + tapsJS + holdJS + delayJS + btnCmdJS + interactiveListJS + stopListJS +
        "const VIDEO_ENABLED=" + String(videoEnabled ? "true" : "false") + ";"
        "const VIDEO_PAUSED=" + String(videoPaused ? "true" : "false") + ";"
        "const SNAP_FPS=" + String(snapshotFPS) + ";"
        // WS auth token — injected ONLY when auth is enabled. Because the
        // page itself is served behind HTTP auth, only authenticated users ever
        // receive this token.
        "const WS_AUTH=" + String(authEnabled ? "true" : "false") + ";"
        "const WS_TOKEN=\"" + (authEnabled ? jsEscape(wsToken) : String("")) + "\";"
        //  encryption constants. WS_ENCRYPT enables AES-256-GCM of commands.
        // The salt is public (PBKDF2 salt); the passphrase is NEVER sent — the
        // user types it into the browser, so plaintext and key never hit the wire.
        "const WS_ENCRYPT=" + String(encryptionEnabled ? "true" : "false") + ";"
        "const ENC_SALT=\"" + (encryptionEnabled ? jsEscape(pbkdfSalt) : String("")) + "\";"
        "const ENC_FALLBACK=" + String(encryptionFallback ? "true" : "false") + ";"
        R"JS(
// Use current origin (works in AP, STA, and Dual), auto-select ws/wss
const gateway = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws';
let ws;
let wsAuthed = false;
let wsGaveUp = false;   // token respins definitiv: nu mai reconectam

// ----  encryption (AES-256-GCM via Web Crypto) ----
// crypto.subtle exists ONLY in a secure context (HTTPS, localhost, tunnel).
// Over plain http://IP it is undefined, so we detect that and refuse to send,
// telling the user to use TLS/localhost. This is a browser rule, not a bug.
let encKey = null;
const cryptoOK = (typeof crypto !== 'undefined' && crypto.subtle);
async function deriveKey(passphrase, saltHex){
  const enc = new TextEncoder();

  const salt = enc.encode(saltHex);
  const baseKey = await crypto.subtle.importKey('raw', enc.encode(passphrase),
                    {name:'PBKDF2'}, false, ['deriveKey']);
  return crypto.subtle.deriveKey(
    {name:'PBKDF2', salt, iterations:100000, hash:'SHA-256'},
    baseKey, {name:'AES-GCM', length:256}, false, ['encrypt']);
}
function b64(bytes){ let s=''; bytes.forEach(b=>s+=String.fromCharCode(b)); return btoa(s); }
async function encryptCmd(plain){
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ctBuf = await crypto.subtle.encrypt({name:'AES-GCM', iv}, encKey,
                    new TextEncoder().encode(plain));
  const ct = new Uint8Array(ctBuf);
  const out = new Uint8Array(iv.length + ct.length);
  out.set(iv, 0); out.set(ct, iv.length);   // [IV][ciphertext+tag]
  return 'enc:' + b64(out);
}
// Afiseaza vizibil starea canalului. Utilizatorul stie mereu daca
// comenzile sunt criptate sau nu — transparenta, nu presupuneri.
let plainMode = false;
function secBadge(text, bg, fg){
  let b = document.getElementById('secBadge');
  if(!b){
    b = document.createElement('div');
    b.id = 'secBadge';
    b.style.cssText = 'margin:10px auto 0;padding:6px 16px;border-radius:20px;'
      + 'display:inline-block;font-size:13px;font-weight:700;letter-spacing:.5px;';
    document.body.insertBefore(b, document.body.firstChild);
  }
  b.textContent = text;
  b.style.background = bg;
  b.style.color = fg;
}

async function initEncryption(){
  if(!WS_ENCRYPT){ console.log('[EWRC] Criptare dezactivata in sketch.'); return; }
  console.log('[EWRC] WS_ENCRYPT=' + WS_ENCRYPT + '  FALLBACK=' + ENC_FALLBACK
            + '  crypto.subtle=' + (typeof crypto !== 'undefined' ? typeof crypto.subtle : 'no crypto')
            + '  secureContext=' + (typeof isSecureContext !== 'undefined' ? isSecureContext : '?'));
  if(!cryptoOK){

    if(ENC_FALLBACK){
      plainMode = true;
      secBadge('CANAL NECRIPTAT - context nesecurizat', '#7a2c2c', '#ffdcdc');
      console.log('Web Crypto indisponibil: comenzi in clar (fallback activat).');
    } else {
      secBadge('BLOCAT - criptarea necesita HTTPS/localhost', '#7a2c2c', '#ffdcdc');
      alert('Criptarea necesita un context securizat (HTTPS sau localhost). '
          + 'Peste http://IP simplu, browserul dezactiveaza Web Crypto.');
    }
    return;
  }
  const pass = prompt('Introduceti fraza de criptare:');
  if(pass){
    encKey = await deriveKey(pass, ENC_SALT);
    secBadge('CANAL CRIPTAT AES-256-GCM', '#14532d', '#c9f7d5');
  } else if(ENC_FALLBACK){
    plainMode = true;
    secBadge('CANAL NECRIPTAT - fara fraza', '#7a2c2c', '#ffdcdc');
  } else {
    secBadge('BLOCAT - fara fraza de criptare', '#7a2c2c', '#ffdcdc');
  }
}

function connectWS(){
  ws = new WebSocket(gateway);
  ws.onopen  = ()=>{
    if (WS_AUTH) { wsAuthed = false; ws.send('auth:' + WS_TOKEN); }
    else { wsAuthed = true; }
  };
  ws.onmessage = (ev)=>{
    if (ev.data === 'auth:ok')      wsAuthed = true;

    if (ev.data === 'auth:required'){
      wsAuthed = false;
      if (WS_AUTH && !wsGaveUp) ws.send('auth:' + WS_TOKEN);
    }

    if (ev.data === 'auth:fail' || ev.data === 'auth:expired'){
      wsAuthed = false;
      wsGaveUp = true;
      secBadge('SESIUNE INVALIDA - reincarca pagina (F5)', '#7a2c2c', '#ffdcdc');
      console.log('[EWRC] Token respins sau sesiune expirata. Reincarca pagina.');
    }
  };
  ws.onclose = ()=>{
    wsAuthed = false;
    if (wsGaveUp) return;            // nu mai insistam cu un token invalid
    setTimeout(connectWS,1000);
  };
  ws.onerror = (e)=>console.log('ws err', e);
}
initEncryption();
connectWS();
async function sendCommand(cmd){
  if(!(ws && ws.readyState===WebSocket.OPEN && (wsAuthed || !WS_AUTH))) return;
  if(WS_ENCRYPT){
    if(encKey){
      // Avem cheie: criptam intotdeauna.
      try { ws.send(await encryptCmd(cmd)); } catch(e){ console.log('enc err', e); }
      return;
    }
    // Fara cheie: trimitem in clar DOAR daca fallback-ul e activat explicit.
    if(!plainMode) return;   // strict: refuzam sa trimitem necriptat
  }
  ws.send(cmd);
}

// Resolve the command string for a button id (falls back to the id itself).
function cmdFor(btnId){ return (btnCommands && btnCommands[btnId]!==undefined) ? btnCommands[btnId] : btnId; }

const pressActive={}; const pressStart={}; const holdTimer={}; const actionActive={};
const startDelayTimer={}; const tapCount={}; const tapResetTimer={}; const pendingStopTimer={};
const TAP_RESET_MS=600;

function clearPendingStop(btnId){if(pendingStopTimer[btnId]){clearTimeout(pendingStopTimer[btnId]); pendingStopTimer[btnId]=null;}}
function clearStartDelay(btnId){if(startDelayTimer[btnId]){clearTimeout(startDelayTimer[btnId]); startDelayTimer[btnId]=null;}}
function clearHoldTimeout(btnId){if(holdTimer[btnId]){clearTimeout(holdTimer[btnId]); holdTimer[btnId]=null;}}
function clearTapReset(btnId){if(tapResetTimer[btnId]){clearTimeout(tapResetTimer[btnId]); tapResetTimer[btnId]=null;}}

function scheduleStopIfNeeded(btnId){
  clearPendingStop(btnId);
  const t = (actionTimers && actionTimers[btnId]!==undefined)?actionTimers[btnId]:0;
  if(t===0){ sendCommand("stop"); actionActive[btnId]=false; }
  else if(t>0){ pendingStopTimer[btnId]=setTimeout(()=>{ sendCommand("stop"); pendingStopTimer[btnId]=null; actionActive[btnId]=false; }, t); }
  // t==-1 => run until manual stop
}

function startActionForButton(btnId, triggeredBy){
  clearStartDelay(btnId);
  const delayMs    = (delayConfig && delayConfig[btnId]!==undefined)?delayConfig[btnId]:0;
  const actionTimer= (actionTimers && actionTimers[btnId]!==undefined)?actionTimers[btnId]:0;

  function doStart(){
    const btn=document.querySelector('button[data-id="'+btnId+'"]');
    if(btn) btn.classList.add("pressed");
    sendCommand(cmdFor(btnId));
    actionActive[btnId]=true;
    if(triggeredBy==="release" && actionTimer>0){
      clearPendingStop(btnId);
      pendingStopTimer[btnId]=setTimeout(()=>{
        sendCommand("stop"); pendingStopTimer[btnId]=null; actionActive[btnId]=false; if(btn) btn.classList.remove("pressed");
      }, actionTimer);
    }
  }

  if(delayMs>0){
    startDelayTimer[btnId]=setTimeout(()=>{
      startDelayTimer[btnId]=null;
      if(triggeredBy==="press" && !pressActive[btnId]) return;
      doStart();
    }, delayMs);
  } else doStart();
}

// Attach behavior to every interactive (non-stop) button.
interactiveButtons.forEach(id=>{
  pressActive[id]=false; pressStart[id]=0; holdTimer[id]=null; actionActive[id]=false;
  startDelayTimer[id]=null; tapCount[id]=0; tapResetTimer[id]=null; pendingStopTimer[id]=null;
  const el=document.querySelector('button[data-id="'+id+'"]');
  if(!el) return;

  function onDown(e){ e&&e.preventDefault&&e.preventDefault(); pressActive[id]=true; pressStart[id]=Date.now(); clearPendingStop(id);
    const tapsNeeded=(tapConfig && tapConfig[id]!==undefined)?tapConfig[id]:0;
    const holdMs=(holdConfig && holdConfig[id]!==undefined)?holdConfig[id]:0;
    if(tapsNeeded===0){
      if(holdMs===0) startActionForButton(id,"press");
      else{ clearHoldTimeout(id); holdTimer[id]=setTimeout(()=>{ holdTimer[id]=null; if(pressActive[id]) startActionForButton(id,"press"); }, holdMs); }
    } else clearHoldTimeout(id);
  }

  function onUp(e){ e&&e.preventDefault&&e.preventDefault(); const now=Date.now(); const elapsed=now-(pressStart[id]||now); pressActive[id]=false; clearHoldTimeout(id);
    const tapsNeeded=(tapConfig && tapConfig[id]!==undefined)?tapConfig[id]:0;
    const holdMs=(holdConfig && holdConfig[id]!==undefined)?holdConfig[id]:0;

    if(tapsNeeded>0){
      if(holdMs===0 || elapsed>=holdMs){
        tapCount[id]=(tapCount[id]||0)+1; clearTapReset(id); tapResetTimer[id]=setTimeout(()=>{ tapCount[id]=0; tapResetTimer[id]=null; }, TAP_RESET_MS);
        if(tapCount[id]>=tapsNeeded){ startActionForButton(id,"release"); clearTapReset(id); tapCount[id]=0; }
      }
      return;
    }

    if(actionActive[id]){ scheduleStopIfNeeded(id); actionActive[id]=false; clearStartDelay(id); const btn=document.querySelector('button[data-id="'+id+'"]'); if(btn) btn.classList.remove("pressed"); return; }
    if(startDelayTimer[id]){ clearStartDelay(id); return; }
    if(holdMs>0 && elapsed<holdMs) return;
    scheduleStopIfNeeded(id); const btn=document.querySelector('button[data-id="'+id+'"]'); if(btn) btn.classList.remove("pressed");
  }

  function onLeave(e){ if(pressActive[id]) onUp(e); }

  el.addEventListener('mousedown', onDown);
  el.addEventListener('touchstart', onDown, {passive:false});
  el.addEventListener('mouseup', onUp);
  el.addEventListener('touchend', onUp);
  el.addEventListener('mouseleave', onLeave);
});

// STOP buttons (one or more) — cancel all timers and broadcast stop.
function stopAll(){
  Object.keys(startDelayTimer).forEach(k=>{ if(startDelayTimer[k]){ clearTimeout(startDelayTimer[k]); startDelayTimer[k]=null; } });
  Object.keys(pendingStopTimer).forEach(k=>{ if(pendingStopTimer[k]){ clearTimeout(pendingStopTimer[k]); pendingStopTimer[k]=null; } });
  Object.keys(tapResetTimer).forEach(k=>{ if(tapResetTimer[k]){ clearTimeout(tapResetTimer[k]); tapResetTimer[k]=null; } });
  Object.keys(holdTimer).forEach(k=>{ if(holdTimer[k]){ clearTimeout(holdTimer[k]); holdTimer[k]=null; } });
  Object.keys(pressActive).forEach(k=>{
    pressActive[k]=false; actionActive[k]=false; tapCount[k]=0;
    const btn=document.querySelector('button[data-id="'+k+'"]'); if(btn) btn.classList.remove("pressed");
  });
  sendCommand("stop");
}
stopButtons.forEach(id=>{
  const stopBtn=document.querySelector('button[data-id="'+id+'"]');
  if(!stopBtn) return;
  stopBtn.addEventListener('mousedown', stopAll);
  stopBtn.addEventListener('touchstart', stopAll, {passive:false});
});

// PWM slider
const slider=document.getElementById("pwmSlider");
const pwmValue=document.getElementById("pwmValue");
if(slider){
  slider.addEventListener("input", ()=>{
    pwmValue.textContent=slider.value;
    sendCommand("pwm:"+slider.value);
  });
}

// Snapshot refresh
(function(){
  if(!VIDEO_ENABLED) return;
  const img = document.getElementById('snapImg');
  if(!img) return;
  if(VIDEO_PAUSED || !SNAP_FPS || SNAP_FPS===0) return;
  const period = Math.max(200, Math.floor(1000/Math.min(SNAP_FPS, 30)));
  // Cand nu exista inca un cadru, serverul raspunde 204 No Content, iar
  // browserul ar afisa pictograma de imagine stricata. Stergem sursa la eroare,
  // asa incat caseta ramane curata pana la primul cadru valid.
  img.onerror = function(){ img.removeAttribute('src'); };
  function tick(){
    img.src = '/snapshot?t=' + Date.now();
  }
  tick();
  setInterval(tick, period);
})();
)JS"
"</script></body></html>";

    return page;
}