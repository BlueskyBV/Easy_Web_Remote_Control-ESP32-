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
    ar_reconnectWindowMs = reconnectWindowMs; // 0=never, <0=forever, >0=ms window
    ar_rebootAfterMs     = rebootAfterMs;     // 0=disabled, >0=ms to reboot
    ar_checkIntervalMs   = checkIntervalMs;
    ar_reconnectAfterMs  = reconnectAfterMs;
    ar_reconnectPeriodMs = reconnectPeriodMs;
}

// -------- bring-up: AP / STA / Dual --------
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
        // leave staDisconnectSinceMs = 0; it will be set on first check
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

    // AP side
    if (apStaticSet) {
        if (!WiFi.softAPConfig(apIP, apGW, apSN)) {
            Serial.println("[AP] softAPConfig failed; using defaults.");
        }
    }
    WiFi.softAP(apSsid, apPassword);
    Serial.print("[AP] IP: ");
    Serial.println(WiFi.softAPIP());

    // STA side
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

// -------- server wiring --------
void EasyWebRemoteControl::startServer() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    addHttpRoutes();

    server.begin();
    Serial.println("[HTTP] Server started.");
}

void EasyWebRemoteControl::addHttpRoutes() {
    // Main UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", instance->buildHtmlPage());
    });

    // Snapshot endpoint
    server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest *req){
        if (!instance) { req->send(500, "text/plain", "No instance"); return; }
        if (!instance->videoEnabled || instance->videoPaused || !instance->frameProvider) {
            req->send(204); // No Content
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

// -------- update --------
void EasyWebRemoteControl::update() {
    ws.cleanupClients();
    serviceWatchdog();
    checkAndRecoverWiFi();
    printUrlsIfChanged(false);


    

    // ✅ NEW: automatically reprint IP if STA connects later
    if (WiFi.getMode() & WIFI_MODE_STA) {
        static bool printed = false;
        if (WiFi.status() == WL_CONNECTED && !printed) {
            Serial.print("[STA] Connected, IP: ");
            Serial.println(WiFi.localIP());
            printed = true;
        }
    }

}

// -------- URL printing helpers --------
void EasyWebRemoteControl::printUrlsIfChanged(bool forceOnce) {
    IPAddress ap = WiFi.softAPIP();
    IPAddress st = WiFi.localIP();

    bool apValid = (ap[0] != 0 || ap[1] != 0 || ap[2] != 0 || ap[3] != 0);
    bool stValid = (st[0] != 0 || st[1] != 0 || st[2] != 0 || st[3] != 0);

    // Print AP URL block
    if (forceOnce || (apValid && ap != lastApIPPrinted)) {
        lastApIPPrinted = ap;
        Serial.println("[URL] AP:");
        printAddrLine("  HTTP", ap);
        printAddrLine("  WS  ", ap);
    }

    // Print STA URL block
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

// -------- Auto-recovery internals --------
void EasyWebRemoteControl::initWatchdogIfNeeded() {
#ifdef ESP32
    if (wdtInited) return;

    esp_err_t err;

    // First, try to simply subscribe the current (loop) task to an already-initialized WDT.
    err = esp_task_wdt_add(NULL);
    if (err == ESP_OK) {
        // WDT was already initialized elsewhere; we just subscribed.
        wdtInited = true;
    } else {
        // Not initialized yet? Initialize it, then subscribe.
      #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
        esp_task_wdt_config_t cfg = {};
        cfg.timeout_ms     = 8000;  // 8s
        cfg.idle_core_mask = 0;     // don't auto-subscribe idle tasks
        cfg.trigger_panic  = true;
        if (esp_task_wdt_init(&cfg) == ESP_OK) {
            if (esp_task_wdt_add(NULL) == ESP_OK) {
                wdtInited = true;
            }
        }
      #else
        if (esp_task_wdt_init(8, true) == ESP_OK) {  // 8s, panic on timeout
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

    // Throttle checks
    if (now - lastWifiCheckMs < ar_checkIntervalMs) return;
    lastWifiCheckMs = now;

    if (!autoRecoveryEnabled) return;

    if (WiFi.getMode() & WIFI_MODE_STA) {
        wl_status_t s = WiFi.status();

        // ====== CONNECTED NOW ======
        if (s == WL_CONNECTED) {
            // If we were previously disconnected, announce recovery once
            if (staDisconnectSinceMs != 0 || lastStaSeenConnectedMs == 0) {
                Serial.println("[STA] Reconnected successfully.");
                printUrlsIfChanged(true); // show usable IPs immediately
            }
            lastStaSeenConnectedMs = now;
            staDisconnectSinceMs   = 0;
            return;
        }

        // ====== DISCONNECTED NOW ======
        // mark the moment we first observed disconnect & announce it once
        if (staDisconnectSinceMs == 0) {
            staDisconnectSinceMs = now;
            Serial.println("[STA] Lost connection, trying to recover...");
        }

        const unsigned long disconnectedFor = now - staDisconnectSinceMs;

        // Auto-reboot if configured
        if (ar_rebootAfterMs > 0 && disconnectedFor >= ar_rebootAfterMs) {
            Serial.println("[STA] Disconnected too long; rebooting for recovery...");
            ESP.restart();
            return;
        }

        // If user disabled reconnects entirely
        if (ar_reconnectWindowMs == 0) return;

        // If a finite reconnect window was set and we've exceeded it, stop trying
        if (ar_reconnectWindowMs > 0 &&
            disconnectedFor > (unsigned long)ar_reconnectWindowMs) {
            return;
        }

        // Only start trying after the grace delay
        if (disconnectedFor < ar_reconnectAfterMs) return;

        // Try at most once per ar_reconnectPeriodMs
        if (now - lastReconnectAttemptMs >= ar_reconnectPeriodMs) {
            lastReconnectAttemptMs = now;
            Serial.println("[STA] Attempting auto-reconnect...");
            WiFi.reconnect();
        }
    }
}

// -------- public API: callbacks --------
void EasyWebRemoteControl::onFront(void (*func)()) { frontCallback = func; }
void EasyWebRemoteControl::onBack(void (*func)())  { backCallback  = func; }
void EasyWebRemoteControl::onLeft(void (*func)())  { leftCallback  = func; }
void EasyWebRemoteControl::onRight(void (*func)()) { rightCallback = func; }
void EasyWebRemoteControl::onStop(void (*func)())  { stopCallback  = func; }

// -------- PWM API --------
int  EasyWebRemoteControl::getPWM() { return currentPWM; }
void EasyWebRemoteControl::setInitialPWM(int val) { if(val<0) val=0; if(val>255) val=255; currentPWM=val; }
void EasyWebRemoteControl::showSlider(bool enable) { sliderEnabled = enable; }

// -------- video controls --------
void EasyWebRemoteControl::enableVideo(bool enable) { videoEnabled = enable; }
void EasyWebRemoteControl::setSnapshotFPS(uint8_t fps) { snapshotFPS = fps; }
void EasyWebRemoteControl::pauseSnapshots(bool paused) { videoPaused = paused; }
void EasyWebRemoteControl::setVideoFrameProvider(VideoProvider p) { frameProvider = p; }

// -------- per-button behavior --------
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

// -------- command handling --------
void EasyWebRemoteControl::handleCommand(String cmd){
    cmd.trim();
    if (cmd.length()==0) return;

    if (cmd.startsWith("pwm:")) {
        int v = cmd.substring(4).toInt();
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        currentPWM = v;
        return;
    }

    if      (cmd=="front" && frontCallback)  frontCallback();
    else if (cmd=="back"  && backCallback)   backCallback();
    else if (cmd=="left"  && leftCallback)   leftCallback();
    else if (cmd=="right" && rightCallback)  rightCallback();
    else if (cmd=="stop"  && stopCallback)   stopCallback();
}

// -------- websocket --------
void EasyWebRemoteControl::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (!instance) return;
    if (type == WS_EVT_DATA) {
        String msg;
        msg.reserve(len);
        for (size_t i=0; i<len; ++i) msg += (char)data[i];
        instance->handleCommand(msg);
    }
}

// -------- page builder --------
String EasyWebRemoteControl::buildHtmlPage(){
    // Serialize configuration maps to JS objects
    String timersJS = "const actionTimers={";
    for (auto &kv: actionTimers) { timersJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!actionTimers.empty()) timersJS.remove(timersJS.length()-1);
    timersJS += "};";

    String tapsJS = "const tapConfig={";
    for (auto &kv: tapSettings) { tapsJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!tapSettings.empty()) tapsJS.remove(timersJS.length()-1);
    tapsJS += "};";

    String holdJS = "const holdConfig={";
    for (auto &kv: holdSettings) { holdJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!holdSettings.empty()) holdJS.remove(timersJS.length()-1); // safe anyway if empty
    holdJS += "};";

    String delayJS = "const delayConfig={";
    for (auto &kv: delaySettings) { delayJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!delaySettings.empty()) delayJS.remove(delayJS.length()-1);
    delayJS += "};";

    // Slider block with current value
    String sliderUI = "";
    if (sliderEnabled) {
        sliderUI =
            "<div class=\"row\">"
              "<input type=\"range\" id=\"pwmSlider\" min=\"0\" max=\"255\" value=\"" + String(currentPWM) + "\">"
              "<span id=\"pwmValue\">" + String(currentPWM) + "</span>"
            "</div>";
    }

    // Video block (simple snapshot <img>)
    String videoUI = "";
    if (videoEnabled) {
        videoUI =
            "<div class=\"row\" id=\"videoRow\">"
              "<img id=\"snapImg\" alt=\"camera\" style=\"max-width:320px;max-height:240px;border-radius:8px;box-shadow:0 1px 4px rgba(0,0,0,.2)\"/>"
            "</div>";
    }

    // Full page with JS
    String page =
      "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Remote Control</title>"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<style>"
      "body{text-align:center;font-family:Arial,Helvetica,sans-serif;background:#f0f0f0;margin:0;padding:12px;-webkit-touch-callout:none;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;}"
      "h2{margin:18px 0;font-size:20px;}"
      ".row{margin:14px 0;display:flex;justify-content:center;align-items:center;gap:12px;-webkit-tap-highlight-color:transparent;}"
      "button{width:100px;height:100px;font-size:36px;margin:0;border-radius:12px;cursor:pointer;background:#4CAF50;color:white;border:none;transition:0.18s;}"
      "button.pressed{background:#388E3C;transform:translateY(1px);}"
      "button:hover{background-color:#45a049;}"
      "#middleRow{display:flex;gap:12px;align-items:center;justify-content:center;}"
      "input[type=range]{width:220px;}#pwmValue{display:inline-block;width:44px;text-align:center;font-size:18px;margin-left:10px;}"
      "@media(max-width:480px){button{width:80px;height:80px;font-size:28px;}input[type=range]{width:180px;}}"
      "</style></head><body>"
      "<h2>Remote Control</h2><div id=\"controls\">"
      "<div class=\"row\"><button id=\"front\">↑</button></div>"
      "<div id=\"middleRow\" class=\"row\"><button id=\"left\">←</button><button id=\"stop\">■</button><button id=\"right\">→</button></div>"
      "<div class=\"row\"><button id=\"back\">↓</button></div>"
      + sliderUI + videoUI +
      "<script>" + timersJS + tapsJS + holdJS + delayJS +
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

// Buttons
const buttons=[
  {id:"front", cmd:"front"},
  {id:"back",  cmd:"back"},
  {id:"left",  cmd:"left"},
  {id:"right", cmd:"right"}
];

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
    const btn=document.getElementById(btnId);
    if(btn) btn.classList.add("pressed");
    sendCommand(btnId);
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

buttons.forEach(b=>{
  const id=b.id;
  pressActive[id]=false; pressStart[id]=0; holdTimer[id]=null; actionActive[id]=false;
  startDelayTimer[id]=null; tapCount[id]=0; tapResetTimer[id]=null; pendingStopTimer[id]=null;
  const el=document.getElementById(id);

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

    if(actionActive[id]){ scheduleStopIfNeeded(id); actionActive[id]=false; clearStartDelay(id); const btn=document.getElementById(id); if(btn) btn.classList.remove("pressed"); return; }
    if(startDelayTimer[id]){ clearStartDelay(id); return; }
    if(holdMs>0 && elapsed<holdMs) return;
    scheduleStopIfNeeded(id); const btn=document.getElementById(id); if(btn) btn.classList.remove("pressed");
  }

  function onLeave(e){ if(pressActive[id]) onUp(e); }

  el.addEventListener('mousedown', onDown);
  el.addEventListener('touchstart', onDown, {passive:false});
  el.addEventListener('mouseup', onUp);
  el.addEventListener('touchend', onUp);
  el.addEventListener('mouseleave', onLeave);
});

// STOP button
const stopBtn=document.getElementById("stop");
function stopAll(){
  Object.keys(startDelayTimer).forEach(k=>{ if(startDelayTimer[k]){ clearTimeout(startDelayTimer[k]); startDelayTimer[k]=null; } });
  Object.keys(pendingStopTimer).forEach(k=>{ if(pendingStopTimer[k]){ clearTimeout(pendingStopTimer[k]); pendingStopTimer[k]=null; } });
  Object.keys(tapResetTimer).forEach(k=>{ if(tapResetTimer[k]){ clearTimeout(tapResetTimer[k]); tapResetTimer[k]=null; } });
  Object.keys(holdTimer).forEach(k=>{ if(holdTimer[k]){ clearTimeout(holdTimer[k]); holdTimer[k]=null; } });
  Object.keys(pressActive).forEach(k=>{
    pressActive[k]=false; actionActive[k]=false; tapCount[k]=0;
    const btn=document.getElementById(k); if(btn) btn.classList.remove("pressed");
  });
  sendCommand("stop"); if(stopBtn) stopBtn.classList.remove("pressed");
}
stopBtn.addEventListener('mousedown', stopAll);
stopBtn.addEventListener('touchstart', stopAll);

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
  const period = Math.max(200, Math.floor(1000/Math.min(SNAP_FPS, 30))); // clamp to <=30fps, >=5Hz timer
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
