#include "pt/pt_display.h"
#include "ui/App.h"

#include <WiFi.h>
#include <esp_wifi.h>

static CupboardApp app;

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("PandaFarm boot");
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);
    pt_setup_display(PT_LVGL_RENDER_PARTIAL_2_PSRAM);
    app.begin();
}

void loop() {
    app.loop();
}
