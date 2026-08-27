#include "storage/Preferences.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>

CupboardPreferences &CupboardPreferences::instance() {
    static CupboardPreferences inst;
    return inst;
}

void CupboardPreferences::takeLock() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (lock_) xSemaphoreTake(lock_, portMAX_DELAY);
}

void CupboardPreferences::giveLock() {
    if (lock_) xSemaphoreGive(lock_);
}

void CupboardPreferences::begin() {
    takeLock();
    if (!loaded_) {
        // Keep the original NVS namespace so upgrades do not wipe WiFi/printers.
        prefs_.begin("PandaCupboard", false);
        loaded_ = true;
    }
    giveLock();
}

bool CupboardPreferences::putStringIfChanged(const char *key, const char *value) {
    const char *v = value ? value : "";
    char stored[80] = {};
    prefs_.getString(key, stored, sizeof(stored));
    if (strcmp(stored, v) == 0) return false;
    prefs_.putString(key, v);
    return true;
}

void CupboardPreferences::snapshotRadioWifi(const char *ssid, const char *password) {
    if (!ssid || !ssid[0]) return;
    wifi_config_t cfg{};
    const size_t ssidLen = strnlen(ssid, sizeof(cfg.sta.ssid));
    memcpy(cfg.sta.ssid, ssid, ssidLen);
    if (password) {
        const size_t passLen = strnlen(password, sizeof(cfg.sta.password));
        memcpy(cfg.sta.password, password, passLen);
    }
    // Temporarily store in flash so a power cycle can recover if our namespace is torn.
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        Serial.printf("[NVS] radio WiFi snapshot failed err=%d\n", static_cast<int>(err));
    }
}

void CupboardPreferences::writeWifiIfPresent(const WifiConfig &wifi) {
    if (!wifi.ssid[0]) {
        Serial.println("[NVS] keeping stored WiFi (empty RAM credentials)");
        return;
    }

    char storedSsid[33] = {};
    prefs_.getString("wifi_ssid", storedSsid, sizeof(storedSsid));
    const bool ssidChanged = strcmp(storedSsid, wifi.ssid) != 0;
    const bool wroteSsid = putStringIfChanged("wifi_ssid", wifi.ssid);

    bool wrotePass = false;
    if (wifi.password[0] || ssidChanged) {
        wrotePass = putStringIfChanged("wifi_pass", wifi.password);
    }

    if (wroteSsid || wrotePass) {
        snapshotRadioWifi(wifi.ssid, wifi.password);
        Serial.printf("[NVS] stored WiFi ssid=\"%s\"\n", wifi.ssid);
    }
}

void CupboardPreferences::writeMeta(const AppConfig &cfg) {
    prefs_.putInt("p_count", cfg.printerCount);
    prefs_.putInt("p_edit", cfg.editIndex);
    prefs_.putBool("dark", cfg.darkTheme);
    prefs_.putUChar("theme", static_cast<uint8_t>(cfg.uiTheme));
    prefs_.putUChar("text", static_cast<uint8_t>(cfg.textSize));
    prefs_.putUChar("bright", cfg.brightness);
    prefs_.putUShort("dim", cfg.dimSec);
    prefs_.putUShort("sleep", cfg.sleepSec);
    prefs_.putUChar("sort", static_cast<uint8_t>(cfg.fleetSort));
}

void CupboardPreferences::writePrintersLocked(const AppConfig &cfg) {
    const int count = cfg.printerCount < BAMBU_MAX_PRINTERS ? cfg.printerCount : BAMBU_MAX_PRINTERS;
    prefs_.putInt("p_count", cfg.printerCount);
    prefs_.putInt("p_edit", cfg.editIndex);
    for (int i = 0; i < count; ++i) {
        char key[16];
        const PrinterProfile &p = cfg.printers[i];
        snprintf(key, sizeof(key), "p%d_name", i);
        putStringIfChanged(key, p.name);
        snprintf(key, sizeof(key), "p%d_ip", i);
        putStringIfChanged(key, p.ip);
        snprintf(key, sizeof(key), "p%d_code", i);
        putStringIfChanged(key, p.accessCode);
        snprintf(key, sizeof(key), "p%d_sn", i);
        putStringIfChanged(key, p.serial);
        snprintf(key, sizeof(key), "p%d_model", i);
        putStringIfChanged(key, p.model);
        snprintf(key, sizeof(key), "p%d_type", i);
        prefs_.putUChar(key, static_cast<uint8_t>(p.type));
        snprintf(key, sizeof(key), "p%d_port", i);
        prefs_.putUShort(key, p.port);
    }
}

void CupboardPreferences::restoreWifiFromRadio(AppConfig &out) {
    if (out.wifi.ssid[0]) return;

    wifi_config_t cfg{};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) return;
    if (!cfg.sta.ssid[0]) return;

    char ssid[33] = {};
    char pass[65] = {};
    memcpy(ssid, cfg.sta.ssid, sizeof(cfg.sta.ssid));
    memcpy(pass, cfg.sta.password, sizeof(cfg.sta.password));
    ssid[sizeof(ssid) - 1] = '\0';
    pass[sizeof(pass) - 1] = '\0';
    if (!ssid[0]) return;

    strlcpy(out.wifi.ssid, ssid, sizeof(out.wifi.ssid));
    strlcpy(out.wifi.password, pass, sizeof(out.wifi.password));
    writeWifiIfPresent(out.wifi);
    Serial.printf("[NVS] restored WiFi from radio ssid=\"%s\"\n", out.wifi.ssid);
}

void CupboardPreferences::load(AppConfig &out) {
    begin();
    takeLock();
    memset(&out, 0, sizeof(out));
    prefs_.getString("wifi_ssid", out.wifi.ssid, sizeof(out.wifi.ssid));
    prefs_.getString("wifi_pass", out.wifi.password, sizeof(out.wifi.password));
    restoreWifiFromRadio(out);
    out.printerCount = prefs_.getInt("p_count", 0);
    out.editIndex = prefs_.getInt("p_edit", 0);
    out.darkTheme = prefs_.getBool("dark", true);
    out.uiTheme = static_cast<UiTheme>(prefs_.getUChar(
        "theme", static_cast<uint8_t>(out.darkTheme ? UiTheme::Night : UiTheme::Light)));
    if (static_cast<uint8_t>(out.uiTheme) >= static_cast<uint8_t>(UiTheme::Count)) out.uiTheme = UiTheme::Night;
    out.textSize = static_cast<UiTextSize>(prefs_.getUChar("text", static_cast<uint8_t>(UiTextSize::Medium)));
    if (static_cast<uint8_t>(out.textSize) > static_cast<uint8_t>(UiTextSize::Large)) {
        out.textSize = UiTextSize::Medium;
    }
    out.brightness = prefs_.getUChar("bright", 100);
    if (out.brightness < 1) out.brightness = 1;
    if (out.brightness > 100) out.brightness = 100;
    out.dimSec = prefs_.getUShort("dim", 0);
    if (out.dimSec > DISPLAY_TIMEOUT_MAX_SEC) out.dimSec = DISPLAY_TIMEOUT_MAX_SEC;
    out.sleepSec = prefs_.getUShort("sleep", 0);
    if (out.sleepSec > DISPLAY_TIMEOUT_MAX_SEC) out.sleepSec = DISPLAY_TIMEOUT_MAX_SEC;
    out.fleetSort = static_cast<FleetSort>(prefs_.getUChar("sort", static_cast<uint8_t>(FleetSort::DeviceStatus)));

    if (out.printerCount < 0) out.printerCount = 0;
    if (out.printerCount > BAMBU_MAX_PRINTERS) out.printerCount = BAMBU_MAX_PRINTERS;
    if (out.editIndex < 0 || out.editIndex >= out.printerCount) out.editIndex = 0;

    for (int i = 0; i < out.printerCount; ++i) {
        char key[16];
        PrinterProfile &p = out.printers[i];
        snprintf(key, sizeof(key), "p%d_name", i);
        prefs_.getString(key, p.name, sizeof(p.name));
        if (!p.name[0]) strlcpy(p.name, "Printer", sizeof(p.name));
        snprintf(key, sizeof(key), "p%d_ip", i);
        prefs_.getString(key, p.ip, sizeof(p.ip));
        snprintf(key, sizeof(key), "p%d_code", i);
        prefs_.getString(key, p.accessCode, sizeof(p.accessCode));
        snprintf(key, sizeof(key), "p%d_sn", i);
        prefs_.getString(key, p.serial, sizeof(p.serial));
        snprintf(key, sizeof(key), "p%d_model", i);
        prefs_.getString(key, p.model, sizeof(p.model));
        // Missing type key (pre-Klipper firmware) means Bambu Lab.
        snprintf(key, sizeof(key), "p%d_type", i);
        uint8_t type = prefs_.getUChar(key, static_cast<uint8_t>(PrinterType::BambuLab));
        if (type > static_cast<uint8_t>(PrinterType::Klipper)) {
            type = static_cast<uint8_t>(PrinterType::BambuLab);
        }
        p.type = static_cast<PrinterType>(type);
        snprintf(key, sizeof(key), "p%d_port", i);
        p.port = prefs_.getUShort(key, 0);
    }
    giveLock();
}

void CupboardPreferences::save(const AppConfig &cfg) {
    begin();
    takeLock();
    writeWifiIfPresent(cfg.wifi);
    writeMeta(cfg);
    writePrintersLocked(cfg);
    giveLock();
}

void CupboardPreferences::saveWifi(const WifiConfig &wifi) {
    begin();
    takeLock();
    writeWifiIfPresent(wifi);
    giveLock();
}

void CupboardPreferences::savePrinters(const AppConfig &cfg) {
    begin();
    takeLock();
    writePrintersLocked(cfg);
    giveLock();
}

void CupboardPreferences::clearWifi() {
    begin();
    takeLock();
    prefs_.remove("wifi_ssid");
    prefs_.remove("wifi_pass");
    wifi_config_t empty{};
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    Serial.println("[NVS] cleared WiFi credentials");
    giveLock();
}

bool CupboardPreferences::hasWifi() const {
    AppConfig cfg{};
    const_cast<CupboardPreferences *>(this)->load(cfg);
    return cfg.wifi.ssid[0] != '\0';
}

bool CupboardPreferences::hasPrinter() const {
    AppConfig cfg{};
    const_cast<CupboardPreferences *>(this)->load(cfg);
    if (cfg.printerCount <= 0) return false;
    for (int i = 0; i < cfg.printerCount; ++i) {
        if (cfg.printers[i].ip[0]) return true;
    }
    return false;
}
