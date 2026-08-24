#pragma once

#include "bambu/BambuTypes.h"
#include "ui/Theme.h"
#include <Preferences.h>

struct WifiConfig {
    char ssid[33];
    char password[65];
};

struct AppConfig {
    WifiConfig wifi;
    PrinterProfile printers[BAMBU_MAX_PRINTERS];
    int printerCount;
    int editIndex;
    bool darkTheme;
    FleetSort fleetSort;
    UiTheme uiTheme;
    UiTextSize textSize;
    uint8_t brightness;
    uint16_t dimSec;
    uint16_t sleepSec;
};

class CupboardPreferences {
public:
    static CupboardPreferences &instance();

    void begin();
    void load(AppConfig &out);
    void save(const AppConfig &cfg);
    bool hasWifi() const;
    bool hasPrinter() const;

private:
    CupboardPreferences() = default;
    Preferences prefs_;
    bool loaded_ = false;
};
