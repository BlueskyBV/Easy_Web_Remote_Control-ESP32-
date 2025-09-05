#include "EasyWebRemoteControl.h"

EasyWebRemoteControl* EasyWebRemoteControl::instance = nullptr;

EasyWebRemoteControl::EasyWebRemoteControl()
: server(80), ws("/ws"),
  forwardCallback(nullptr), backwardCallback(nullptr),
  leftCallback(nullptr), rightCallback(nullptr),
  stopCallback(nullptr),
  currentSpeed(0), sliderEnabled(true)
{
    instance = this;
}

// ---------- NEW: config setters ----------
void EasyWebRemoteControl::setAPConfig(IPAddress ip, IPAddress gateway, IPAddress subnet) {
    apIP = ip; apGW = gateway; apSN = subnet;
    apCfgSet = true;
}

bool EasyWebRemoteControl::setSTAStatic(IPAddress local, IPAddress gateway, IPAddress subnet,
                                        IPAddress dns1, IPAddress dns2) {
    staIP = local; staGW = gateway; staSN = subnet; staDNS1 = dns1; staDNS2 = dns2;
    staStaticSet = true;
    return true;
}

void EasyWebRemoteControl::setHostname(const char* name) {
    if (name && *name) hostName = name;
}

// ---------- Network bring-up ----------

void EasyWebRemoteControl::beginAP(const char* ssid, const char* password) {
    WiFi.mode(WIFI_AP);
    if (hostName.length()) WiFi.setHostname(hostName.c_str());

    if (apCfgSet) {
        // Change default 192.168.4.1/24 to user-provided
        WiFi.softAPConfig(apIP, apGW, apSN);
    }
    WiFi.softAP(ssid, password);

    Serial.print("AP started. SSID: ");
    Serial.print(ssid);
    Serial.print("  Password: ");
    Serial.println(password);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Open this in your browser: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/");

    startServer();
}

void EasyWebRemoteControl::beginSTA(const char* ssid, const char* password, uint32_t connectTimeoutMs) {
    WiFi.mode(WIFI_STA);
    if (hostName.length()) WiFi.setHostname(hostName.c_str());

    if (staStaticSet) {
        // Apply user static IP instead of DHCP
        WiFi.config(staIP, staGW, staSN, staDNS1, staDNS2);
    } else {
        // DHCP; clear any previous static config
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi (STA): ");
    Serial.print(ssid);
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < connectTimeoutMs) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        Serial.print("STA connected. IP: ");
        Serial.println(ip);
        Serial.print("Open this in your browser: http://");
        Serial.print(ip);
        Serial.println("/");
    } else {
        Serial.println("STA connect timeout. Continuing anyway; server will still start.");
        // We still start the server; if DHCP later succeeds, it's reachable by that IP.
    }

    startServer();
}

void EasyWebRemoteControl::beginDual(const char* apSsid, const char* apPassword,
                                     const char* staSsid, const char* staPassword,
                                     uint32_t connectTimeoutMs) {
    WiFi.mode(WIFI_AP_STA);
    if (hostName.length()) WiFi.setHostname(hostName.c_str());

    // AP side
    if (apCfgSet) {
        WiFi.softAPConfig(apIP, apGW, apSN);
    }
    WiFi.softAP(apSsid, apPassword);

    Serial.print("AP started. SSID: ");
    Serial.print(apSsid);
    Serial.print("  Password: ");
    Serial.println(apPassword);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("AP URL: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/");

    // STA side
    if (staStaticSet) {
        WiFi.config(staIP, staGW, staSN, staDNS1, staDNS2);
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    WiFi.begin(staSsid, staPassword);

    Serial.print("Connecting to WiFi (STA): ");
    Serial.print(staSsid);
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < connectTimeoutMs) {
        delay(100);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        Serial.print("STA connected. IP: ");
        Serial.println(ip);
        Serial.print("STA URL: http://");
        Serial.print(ip);
        Serial.println("/");
    } else {
        Serial.println("STA connect timeout. AP remains active.");
    }

    startServer();
}

void EasyWebRemoteControl::startServer() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", instance->buildHtmlPage());
    });

    server.begin();
    Serial.println("HTTP/WS server started.");
}

void EasyWebRemoteControl::update() {
    ws.cleanupClients();
}

// ---------- Public API (renamed) ----------

void EasyWebRemoteControl::onFront(void (*func)())   { forwardCallback   = func; }
void EasyWebRemoteControl::onBack(void (*func)())    { backwardCallback  = func; }
void EasyWebRemoteControl::onLeft(void (*func)())    { leftCallback      = func; }
void EasyWebRemoteControl::onRight(void (*func)())   { rightCallback     = func; }
void EasyWebRemoteControl::onStop(void (*func)())    { stopCallback      = func; }

int  EasyWebRemoteControl::getPWM()                { return currentSpeed; }
void EasyWebRemoteControl::setInitialPWM(int val)  { if(val<0) val=0; if(val>255) val=255; currentSpeed=val; }
void EasyWebRemoteControl::showSlider(bool enable) { sliderEnabled = enable; }

// ---------- Configs ----------

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

// ---------- Command handling ----------

void EasyWebRemoteControl::handleCommand(String cmd){
    cmd.trim();
    if (cmd.length()==0) return;

    if (cmd.startsWith("speed:")) {
        int v = cmd.substring(6).toInt();
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        currentSpeed = v;
        return;
    }

    if      (cmd=="forward"  && forwardCallback)  forwardCallback();
    else if (cmd=="backward" && backwardCallback) backwardCallback();
    else if (cmd=="left"     && leftCallback)     leftCallback();
    else if (cmd=="right"    && rightCallback)    rightCallback();
    else if (cmd=="stop"     && stopCallback)     stopCallback();
}

// ---------- WebSocket ----------

void EasyWebRemoteControl::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (!instance) return;
    if (type == WS_EVT_DATA) {
        String msg;
        for (size_t i=0; i<len; ++i) msg += (char)data[i];
        instance->handleCommand(msg);
    }
}

// ---------- Page builder ----------

String EasyWebRemoteControl::buildHtmlPage(){
    // Serialize configuration maps to JS objects
    String timersJS = "const actionTimers={";
    for (auto &kv: actionTimers) { timersJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!actionTimers.empty()) timersJS.remove(timersJS.length()-1);
    timersJS += "};";

    String tapsJS = "const tapConfig={";
    for (auto &kv: tapSettings) { tapsJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!tapSettings.empty()) tapsJS.remove(tapsJS.length()-1);
    tapsJS += "};";

    String holdJS = "const holdConfig={";
    for (auto &kv: holdSettings) { holdJS += "\"" + kv.first + "\":" + String(kv.second) + ","; }
    if (!holdSettings.empty()) holdJS.remove(timersJS.length()-1);
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
              "<input type=\"range\" id=\"speedSlider\" min=\"0\" max=\"255\" value=\"" + String(currentSpeed) + "\">"
              "<span id=\"speedValue\">" + String(currentSpeed) + "</span>"
            "</div>";
    }

    // Use current origin (works for AP/STA/Dual; auto ws/wss)
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
      "input[type=range]{width:220px;}#speedValue{display:inline-block;width:44px;text-align:center;font-size:18px;margin-left:10px;}"
      "@media(max-width:480px){button{width:80px;height:80px;font-size:28px;}input[type=range]{width:180px;}}"
      "</style></head><body>"
      "<h2>Remote Control</h2><div id=\"controls\">"
      "<div class=\"row\"><button id=\"forward\">↑</button></div>"
      "<div id=\"middleRow\" class=\"row\"><button id=\"left\">←</button><button id=\"stop\">■</button><button id=\"right\">→</button></div>"
      "<div class=\"row\"><button id=\"backward\">↓</button></div>"
      + sliderUI +
      "<script>" + timersJS + tapsJS + holdJS + delayJS +
R"rawliteral(

// Use current origin for AP/STA/Dual (auto-switch ws/wss)
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
  {id:"forward", cmd:"forward"},
  {id:"backward", cmd:"backward"},
  {id:"left", cmd:"left"},
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

    if(actionActive[id]){ scheduleStopIfNeeded(id); actionActive[id]=false; clearStartDelay(id);
      const btn=document.getElementById(id); if(btn) btn.classList.remove("pressed"); return; }
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

// Stop button
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

// Slider
const slider=document.getElementById("speedSlider");
const speedValue=document.getElementById("speedValue");
if(slider){
  slider.addEventListener("input", ()=>{
    speedValue.textContent=slider.value;
    sendCommand("speed:"+slider.value);
  });
}

)rawliteral"
      "</script></body></html>";

    return page;
}
