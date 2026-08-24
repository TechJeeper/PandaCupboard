#include "storage/Preferences.h"

#include <cstring>

CupboardPreferences &CupboardPreferences::instance() {
    static CupboardPreferences inst;
    return inst;
}

void CupboardPreferences::begin() {
    if (!loaded_) {
        prefs_.begin("PandaCupboard", false);
        loaded_ = true;
    }
}

void CupboardPreferences::load(AppConfig &out) {
    begin();
    memset(&out, 0, sizeof(out));
    strlcpy(out.wifi.ssid, prefs_.getString("wifi_ssid", "").c_str(), sizeof(out.wifi.ssid));
    strlcpy(out.wifi.password, prefs_.getString("wifi_pass", "").c_str(), sizeof(out.wifi.password));
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
    if (out.dimSec > 600) out.dimSec = 600;
    out.sleepSec = prefs_.getUShort("sleep", 0);
    if (out.sleepSec > 1800) out.sleepSec = 1800;
    out.fleetSort = static_cast<FleetSort>(prefs_.getUChar("sort", static_cast<uint8_t>(FleetSort::DeviceStatus)));

    if (out.printerCount < 0) out.printerCount = 0;
    if (out.printerCount > BAMBU_MAX_PRINTERS) out.printerCount = BAMBU_MAX_PRINTERS;
    if (out.editIndex < 0 || out.editIndex >= out.printerCount) out.editIndex = 0;

    for (int i = 0; i < out.printerCount; ++i) {
        char key[16];
        PrinterProfile &p = out.printers[i];
        snprintf(key, sizeof(key), "p%d_name", i);
        strlcpy(p.name, prefs_.getString(key, "Printer").c_str(), sizeof(p.name));
        snprintf(key, sizeof(key), "p%d_ip", i);
        strlcpy(p.ip, prefs_.getString(key, "").c_str(), sizeof(p.ip));
        snprintf(key, sizeof(key), "p%d_code", i);
        strlcpy(p.accessCode, prefs_.getString(key, "").c_str(), sizeof(p.accessCode));
        snprintf(key, sizeof(key), "p%d_sn", i);
        strlcpy(p.serial, prefs_.getString(key, "").c_str(), sizeof(p.serial));
        snprintf(key, sizeof(key), "p%d_model", i);
        strlcpy(p.model, prefs_.getString(key, "").c_str(), sizeof(p.model));
    }
}

void CupboardPreferences::save(const AppConfig &cfg) {
    begin();
    prefs_.putString("wifi_ssid", cfg.wifi.ssid);
    prefs_.putString("wifi_pass", cfg.wifi.password);
    prefs_.putInt("p_count", cfg.printerCount);
    prefs_.putInt("p_edit", cfg.editIndex);
    prefs_.putBool("dark", cfg.darkTheme);
    prefs_.putUChar("theme", static_cast<uint8_t>(cfg.uiTheme));
    prefs_.putUChar("text", static_cast<uint8_t>(cfg.textSize));
    prefs_.putUChar("bright", cfg.brightness);
    prefs_.putUShort("dim", cfg.dimSec);
    prefs_.putUShort("sleep", cfg.sleepSec);
    prefs_.putUChar("sort", static_cast<uint8_t>(cfg.fleetSort));

    const int count = cfg.printerCount < BAMBU_MAX_PRINTERS ? cfg.printerCount : BAMBU_MAX_PRINTERS;
    for (int i = 0; i < count; ++i) {
        char key[16];
        const PrinterProfile &p = cfg.printers[i];
        snprintf(key, sizeof(key), "p%d_name", i);
        prefs_.putString(key, p.name);
        snprintf(key, sizeof(key), "p%d_ip", i);
        prefs_.putString(key, p.ip);
        snprintf(key, sizeof(key), "p%d_code", i);
        prefs_.putString(key, p.accessCode);
        snprintf(key, sizeof(key), "p%d_sn", i);
        prefs_.putString(key, p.serial);
        snprintf(key, sizeof(key), "p%d_model", i);
        prefs_.putString(key, p.model);
    }
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
