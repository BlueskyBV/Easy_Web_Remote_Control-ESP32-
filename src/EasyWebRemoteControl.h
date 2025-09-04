#ifndef EASYWEBREMOTECONTROL_H
#define EASYWEBREMOTECONTROL_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>

class EasyWebRemoteControl {
public:
    EasyWebRemoteControl();
    void begin(const char* ssid, const char* password);
    void update();

    // Callbacks
    void onForward(void (*func)());
    void onBackward(void (*func)());
    void onLeft(void (*func)());
    void onRight(void (*func)());
    void onStop(void (*func)());

    // Slider control
    int getSpeed();
    void setInitialSpeed(int val);
    void showSlider(bool enable);

    // Configs
    void addActionTimer(const char* buttonId, int durationMs);
    void setTaps(const char* buttonId, int tapsRequired);
    void setHold(const char* buttonId, int holdMs);
    void setDelay(const char* buttonId, int delayMs);

private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    static EasyWebRemoteControl* instance;

    // Callbacks
    void (*forwardCallback)();
    void (*backwardCallback)();
    void (*leftCallback)();
    void (*rightCallback)();
    void (*stopCallback)();

    int currentSpeed;
    bool sliderEnabled;

    std::map<String,int> actionTimers;
    std::map<String,int> tapSettings;
    std::map<String,int> holdSettings;
    std::map<String,int> delaySettings;

    void handleCommand(String cmd);
    static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len);
    String buildHtmlPage();
};

#endif