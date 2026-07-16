#include "EasyWebRemoteControl.h"
#ifdef ESP32
  #include "esp_task_wdt.h"
  #include "esp_idf_version.h"
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
    ar_rebootAfterMs     = rebootAfterMs;
    ar_checkIntervalMs   = checkIntervalMs;
    ar_reconnectAfterMs  = reconnectAfterMs;
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
        staDisconnectSinceMs   = 0;
    } else {
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
        staDisconnectSinceMs   = 0;
    } else {
        Serial.println("[STA] connect timeout; AP remains active.");
    }
    startServer();
    initWatchdogIfNeeded();
    printUrlsIfChanged(true);
}

// -------- server wiring (UNCHANGED) --------
void EasyWebRemoteControl::startServer() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    addHttpRoutes();
    server.begin();
    Serial.println("[HTTP] Server started.");
}

void EasyWebRemoteControl::addHttpRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", instance->buildHtmlPage());
    });
    server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest *req){
        if (!instance) { req->send(500, "text/plain", "No instance"); return; }
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
        AsyncWebServerResponse *resp = req->beginResponse_P(200, f.mime ? f.mime : "image/jpeg", f.data, f.len);
        resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        req->send(resp);
        if (f.release) f.release(f.data);
    });
}

// -------- update (UNCHANGED) --------
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

// -------- URL printing helpers (UNCHANGED) --------
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

// -------- Auto-recovery internals (UNCHANGED) --------
void EasyWebRemoteControl::initWatchdogIfNeeded() {
#ifdef ESP32
    if (wdtInited) return;
    esp_err_t err;
    err = esp_task_wdt_add(NULL);
    if (err == ESP_OK) {
        wdtInited = true;
    } else {
      #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
        esp_task_wdt_config_t cfg = {};
        cfg.timeout_ms     = 8000;
        cfg.idle_core_mask = 0;
        cfg.trigger_panic  = true;
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
            staDisconnectSinceMs   = 0;
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

// -------- public API: callbacks (classic — UNCHANGED) --------
void EasyWebRemoteControl::onFront(void (*func)()) { frontCallback = func; }
void EasyWebRemoteControl::onBack(void (*func)())  { backCallback  = func; }
void EasyWebRemoteControl::onLeft(void (*func)())  { leftCallback  = func; }
void EasyWebRemoteControl::onRight(void (*func)()) { rightCallback = func; }
void EasyWebRemoteControl::onStop(void (*func)())  { stopCallback  = func; }

// ====================================================================
// ============  NEW IN v4: generic command callbacks  ================
// ====================================================================
void EasyWebRemoteControl::onCommand(const char* command, void (*func)()) {
    if (!command) return;
    commandCallbacks[String(command)] = func;
}
void EasyWebRemoteControl::onAnyCommand(void (*func)(const String&)) {
    anyCommandCallback = func;
}

// -------- PWM API (UNCHANGED + new slider config) --------
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

// -------- video controls (UNCHANGED) --------
void EasyWebRemoteControl::enableVideo(bool enable) { videoEnabled = enable; }
void EasyWebRemoteControl::setSnapshotFPS(uint8_t fps) { snapshotFPS = fps; }
void EasyWebRemoteControl::pauseSnapshots(bool paused) { videoPaused = paused; }
void EasyWebRemoteControl::setVideoFrameProvider(VideoProvider p) { frameProvider = p; }

// -------- per-button behavior (UNCHANGED) --------
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
// ============  NEW IN v4: UI model management  =====================
// ====================================================================
EasyWebRemoteControl::UIButton* EasyWebRemoteControl::findButton(const char* id) {
    if (!id) return nullptr;
    String target(id);
    for (auto &b : uiButtons) {
        if (b.id == target) return &b;
    }
    return nullptr;
}

void EasyWebRemoteControl::addButton(const char* id, const char* label, const char* command, int row) {
    if (!id) return;
    // If a button with this id already exists, update it instead of duplicating.
    UIButton* existing = findButton(id);
    if (existing) {
        if (label)   existing->label   = label;
        if (command) existing->command = command;
        existing->row = row;
        return;
    }
    UIButton b;
    b.id      = id;
    b.label   = label ? label : "";
    b.command = command ? command : id;   // default command == id
    b.row     = row;
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
    if (bgColor)      b->bgColor      = bgColor;
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
    if (width  > 0) b->width  = width;
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
    if (bgColor)      defBtnColor   = bgColor;
    if (pressedColor) defBtnPressed = pressedColor;
}
void EasyWebRemoteControl::setDefaultButtonSize(int width, int height) {
    if (width  > 0) defBtnWidth  = width;
    if (height > 0) defBtnHeight = height;
}
void EasyWebRemoteControl::setDefaultFontSize(int fontSize) { if (fontSize > 0) defFontSize = fontSize; }
void EasyWebRemoteControl::setDefaultTextColor(const char* color) { if (color) defTextColor = color; }
void EasyWebRemoteControl::setCustomCSS(const char* css) { if (css) customCSS = css; }
void EasyWebRemoteControl::setHeaderHTML(const char* html) { if (html) headerHTML = html; }
void EasyWebRemoteControl::setFooterHTML(const char* html) { if (html) footerHTML = html; }

// ====================================================================
// ============  NEW IN v4: escaping helpers  ========================
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
    UIButton left;  left.id  = "left";  left.label  = "\xE2\x86\x90"; left.command  = "left";  left.row  = 1;
    UIButton stop;  stop.id  = "stop";  stop.label  = "\xE2\x96\xA0"; stop.command  = "stop";  stop.row  = 1; stop.isStop = true;
    UIButton right; right.id = "right"; right.label = "\xE2\x86\x92"; right.command = "right"; right.row = 1;
    UIButton back;  back.id  = "back";  back.label  = "\xE2\x86\x93"; back.command  = "back";  back.row  = 2;

    uiButtons.push_back(front);
    uiButtons.push_back(left);
    uiButtons.push_back(stop);
    uiButtons.push_back(right);
    uiButtons.push_back(back);

    defaultsInjected = true;
}

// -------- command handling (EXTENDED — classic dispatch preserved) --------
void EasyWebRemoteControl::handleCommand(String cmd){
    cmd.trim();
    if (cmd.length()==0) return;

    if (cmd.startsWith("pwm:")) {
        int v = cmd.substring(4).toInt();
        if (v < sliderMin) v = sliderMin;
        if (v > sliderMax) v = sliderMax;
        currentPWM = v;
        return;
    }

    // (1) Classic fixed dispatch — preserved exactly, highest priority.
    if      (cmd=="front" && frontCallback) { frontCallback(); return; }
    else if (cmd=="back"  && backCallback)  { backCallback();  return; }
    else if (cmd=="left"  && leftCallback)  { leftCallback();  return; }
    else if (cmd=="right" && rightCallback) { rightCallback(); return; }
    else if (cmd=="stop"  && stopCallback)  { stopCallback();  return; }

    // (2) NEW: per-command custom handlers registered via onCommand().
    auto it = commandCallbacks.find(cmd);
    if (it != commandCallbacks.end() && it->second) {
        it->second();
        return;
    }

    // (3) NEW: catch-all handler registered via onAnyCommand().
    if (anyCommandCallback) {
        anyCommandCallback(cmd);
        return;
    }
    // Unknown command with no handler: silently ignored (same as before).
}

// -------- websocket (UNCHANGED) --------
void EasyWebRemoteControl::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    (void)server; (void)client; (void)arg; // required by AsyncWebSocket signature, not used here
    if (!instance) return;
    if (type == WS_EVT_DATA) {
        String msg;
        msg.reserve(len);
        for (size_t i=0; i<len; ++i) msg += (char)data[i];
        instance->handleCommand(msg);
    }
}
// ====================================================================
// ============  NEW IN v4: data-driven page builder  ================
// ====================================================================
// This rewrite generates the UI from the uiButtons vector instead of
// hardcoded HTML. If the user never configured anything, ensureDefaultButtons()
// injects the classic 5-button layout, so the output is functionally identical
// to the legacy version. The embedded JavaScript behavior engine (taps / hold /
// delay / action timers / stop-all) is preserved verbatim — only the button
// list it operates on is now generated dynamically.

String EasyWebRemoteControl::buildHtmlPage(){
    // Make sure we have something to render (classic defaults if untouched).
    ensureDefaultButtons();

    // ---- Serialize behavior configuration maps to JS objects ----
    // (Bug fixed vs legacy: each map now uses its OWN length, not timersJS's.)
    String timersJS = "const actionTimers={";
    for (auto &kv: actionTimers) { timersJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!actionTimers.empty()) timersJS.remove(timersJS.length()-1);
    timersJS += "};";

    String tapsJS = "const tapConfig={";
    for (auto &kv: tapSettings) { tapsJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!tapSettings.empty()) tapsJS.remove(tapsJS.length()-1);
    tapsJS += "};";

    String holdJS = "const holdConfig={";
    for (auto &kv: holdSettings) { holdJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!holdSettings.empty()) holdJS.remove(holdJS.length()-1);
    holdJS += "};";

    String delayJS = "const delayConfig={";
    for (auto &kv: delaySettings) { delayJS += "\"" + jsEscape(kv.first) + "\":" + String(kv.second) + ","; }
    if (!delaySettings.empty()) delayJS.remove(delayJS.length()-1);
    delayJS += "};";

    // ---- Build a JS map: command-per-button-id, plus a list of interactive
    //      (non-stop) buttons and the id(s) flagged as "stop". ----
    String btnCmdJS = "const btnCommands={";
    String interactiveListJS = "const interactiveButtons=[";
    String stopListJS = "const stopButtons=[";
    bool firstCmd = true, firstInteractive = true, firstStop = true;
    for (auto &b : uiButtons) {
        String cmd = b.command.length() ? b.command : b.id;
        if (!firstCmd) btnCmdJS += ",";
        btnCmdJS += "\"" + jsEscape(b.id) + "\":\"" + jsEscape(cmd) + "\"";
        firstCmd = false;

        if (b.isStop) {
            if (!firstStop) stopListJS += ",";
            stopListJS += "\"" + jsEscape(b.id) + "\"";
            firstStop = false;
        } else {
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
    for (auto &b : uiButtons) {
        String rule = "";
        if (b.bgColor.length())      rule += "background:" + b.bgColor + ";";
        if (b.textColor.length())    rule += "color:" + b.textColor + ";";
        if (b.width  > 0)            rule += "width:" + String(b.width) + "px;";
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
    for (auto &b : uiButtons) if (b.row > maxRow) maxRow = b.row;

    String controlsHTML = "";
    for (int r = 0; r <= maxRow; ++r) {
        String rowHTML = "";
        bool any = false;
        for (auto &b : uiButtons) {
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
              "<img id=\"snapImg\" alt=\"camera\" style=\"max-width:320px;max-height:240px;border-radius:8px;box-shadow:0 1px 4px rgba(0,0,0,.2)\"/>"
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
      "const VIDEO_PAUSED="  + String(videoPaused  ? "true" : "false") + ";"
      "const SNAP_FPS="      + String(snapshotFPS) + ";"
R"JS(
// Use current origin (works in AP, STA, and Dual), auto-select ws/wss
const gateway = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws';
let ws;
function connectWS(){
  ws = new WebSocket(gateway);
  ws.onopen  = ()=>console.log('ws open');
  ws.onclose = ()=>{ console.log('ws closed'); setTimeout(connectWS,1000); };
  ws.onerror = (e)=>console.log('ws err', e);
}
connectWS();
function sendCommand(cmd){ if(ws && ws.readyState===WebSocket.OPEN) ws.send(cmd); }

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