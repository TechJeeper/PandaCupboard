#pragma once

#include "bambu/BambuTypes.h"
#include "ui/Theme.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
    void saveWifi(const WifiConfig &wifi);
    void savePrinters(const AppConfig &cfg);
    void clearWifi();
    bool hasWifi() const;
    bool hasPrinter() const;

private:
    CupboardPreferences() = default;

    void takeLock();
    void giveLock();
    bool putStringIfChanged(const char *key, const char *value);
    void writeWifiIfPresent(const WifiConfig &wifi);
    void writeMeta(const AppConfig &cfg);
    void writePrintersLocked(const AppConfig &cfg);
    void restoreWifiFromRadio(AppConfig &out);
    static void snapshotRadioWifi(const char *ssid, const char *password);

    Preferences prefs_;
    SemaphoreHandle_t lock_ = nullptr;
    bool loaded_ = false;
};
