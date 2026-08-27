#include "ui/App.h"

#include "klipper/KlipperFleet.h"
#include "pt/pt_display.h"
#include "ui/Keyboard.h"
#include "ui/Notify.h"
#include "ui/Theme.h"

#include <WiFi.h>
#include <esp_system.h>
#include <cstdlib>
#include <cstring>

void CupboardApp::begin() {
    CupboardPreferences::instance().begin();
    CupboardPreferences::instance().load(config_);
    PaxxTheme::set(config_.uiTheme, config_.textSize);
    PaxxTheme::apply();
    pt_display_set_policy(config_.brightness, config_.dimSec, config_.sleepSec);
    buildShell();
    PaxxNotify::init(shell_);
    BambuFleet::instance().begin(&config_, &discovery_);
    KlipperFleet::instance().begin(&config_);

    wifi_.setStatusCallback([this](bool connected, const char *message) {
        if (activeScreen_ == wifiScreen_.root()) {
            const bool joining = wifiScreen_.shouldLeaveOnConnect();
            if (message && message[0] && !wifiScreen_.isScanning() && (joining || (!connected && !wifi_.isConnectPending()))) {
                wifiScreen_.setStatus(message);
            }
            if (!wifi_.isConnectPending() && !wifiScreen_.isScanning()) {
                showGlobalLoading(false);
            }
        }
        if (connected) {
            wifiLostAtMs_ = 0;
            if (config_.wifi.ssid[0]) {
                CupboardPreferences::instance().saveWifi(config_.wifi);
            }
            discovery_.begin();
            if (wifiScreen_.shouldLeaveOnConnect() && activeScreen_ == wifiScreen_.root()) {
                if (CupboardPreferences::instance().hasPrinter()) showFleet();
                else showPrinterType();
            }
        } else if (!wifi_.isConnectPending() && !wifi_.isScanning()) {
            wifiLostAtMs_ = millis();
        }
    });
    wifi_.setScanCallback([this](const std::vector<WifiNetwork> &nets) {
        wifiScreen_.onScanDone(nets);
    });
    wifi_.setScanLifecycle(
        [] {
            BambuFleet::instance().setPaused(true);
            KlipperFleet::instance().setPaused(true);
        },
        [] {
            BambuFleet::instance().setPaused(false);
            KlipperFleet::instance().setPaused(false);
        },
        [] {
            return BambuFleet::instance().isPauseIdle() && KlipperFleet::instance().isPauseIdle();
        });

    if (CupboardPreferences::instance().hasWifi()) {
        wifi_.startConnect(config_.wifi.ssid, config_.wifi.password, 15);
    }

    if (!CupboardPreferences::instance().hasWifi()) {
        showWifi();
    } else if (!CupboardPreferences::instance().hasPrinter()) {
        showPrinterType();
    } else {
        showFleet();
    }

    paxx_ui_refresh();
    applyPendingScreen();
    if (!activeScreen_) {
        showWifi();
        applyPendingScreen();
    }
}

void CupboardApp::saveConfig() {
    config_.darkTheme = PaxxTheme::isDark();
    config_.uiTheme = PaxxTheme::theme();
    config_.textSize = PaxxTheme::textSize();
    CupboardPreferences::instance().save(config_);
}

void CupboardApp::applyAppearance() {
    PaxxTheme::set(config_.uiTheme, config_.textSize);
    PaxxTheme::apply();
    if (shell_) {
        lv_obj_set_style_bg_color(shell_, PaxxTheme::bg(), LV_PART_MAIN);
        lv_obj_set_style_text_color(shell_, PaxxTheme::text(), LV_PART_MAIN);
        lv_obj_set_style_text_font(shell_, PaxxTheme::fontBody(), LV_PART_MAIN);
    }
    if (gearBtn_) lv_obj_set_style_bg_color(gearBtn_, PaxxTheme::accent(), LV_PART_MAIN);
    if (gearMenu_) lv_obj_set_style_bg_color(gearMenu_, PaxxTheme::surface(), LV_PART_MAIN);
    paxx_apply_accent_chrome(shell_);
    fleet_.onEnter();
    themeScreen_.refresh();
    paxx_ui_refresh();
}

void CupboardApp::applyDisplaySettings(uint8_t brightness, uint16_t dimSec, uint16_t sleepSec, bool save) {
    if (brightness < 1) brightness = 1;
    if (brightness > 100) brightness = 100;
    if (dimSec > DISPLAY_TIMEOUT_MAX_SEC) dimSec = DISPLAY_TIMEOUT_MAX_SEC;
    if (sleepSec > DISPLAY_TIMEOUT_MAX_SEC) sleepSec = DISPLAY_TIMEOUT_MAX_SEC;
    config_.brightness = brightness;
    config_.dimSec = dimSec;
    config_.sleepSec = sleepSec;
    pt_display_set_policy(brightness, dimSec, sleepSec);
    if (save) saveConfig();
}

void CupboardApp::setAppearance(UiTheme theme, UiTextSize textSize) {
    config_.uiTheme = theme;
    config_.textSize = textSize;
    PaxxTheme::set(theme, textSize);
    config_.darkTheme = PaxxTheme::isDark();
    applyAppearance();
    saveConfig();
}

bool CupboardApp::addPrinter() {
    if (config_.printerCount >= BAMBU_MAX_PRINTERS) {
        PaxxNotify::show("Printers", "Maximum printers reached");
        return false;
    }
    showPrinterType();
    return true;
}

bool CupboardApp::addPrinter(PrinterType type) {
    if (config_.printerCount >= BAMBU_MAX_PRINTERS) {
        PaxxNotify::show("Printers", "Maximum printers reached");
        return false;
    }
    PrinterProfile &p = config_.printers[config_.printerCount];
    memset(&p, 0, sizeof(p));
    p.type = type;
    snprintf(p.name, sizeof(p.name), "Printer %d", config_.printerCount + 1);
    config_.editIndex = config_.printerCount;
    config_.printerCount += 1;
    saveConfig();
    BambuFleet::instance().reload();
    KlipperFleet::instance().reload();
    editPrinter(config_.editIndex);
    return true;
}

bool CupboardApp::removePrinter(int index) {
    if (index < 0 || index >= config_.printerCount) return false;
    for (int i = index; i < config_.printerCount - 1; ++i) {
        config_.printers[i] = config_.printers[i + 1];
    }
    memset(&config_.printers[config_.printerCount - 1], 0, sizeof(PrinterProfile));
    config_.printerCount -= 1;
    if (config_.editIndex >= config_.printerCount) {
        config_.editIndex = config_.printerCount > 0 ? config_.printerCount - 1 : 0;
    }
    saveConfig();
    BambuFleet::instance().forgetPrinter(index);
    KlipperFleet::instance().forgetPrinter(index);
    return true;
}

void CupboardApp::editPrinter(int index) {
    if (index < 0 || index >= config_.printerCount) return;
    config_.editIndex = index;
    saveConfig();
    showSetup();
}

void CupboardApp::savePrinterFromSetup() {
    if (config_.editIndex < 0 || config_.editIndex >= config_.printerCount) {
        if (config_.printerCount >= BAMBU_MAX_PRINTERS) return;
        config_.editIndex = config_.printerCount;
        config_.printerCount += 1;
    }
    PrinterProfile &p = config_.printers[config_.editIndex];
    strlcpy(p.name, lv_textarea_get_text(setup_.nameInput()), sizeof(p.name));
    strlcpy(p.ip, lv_textarea_get_text(setup_.ipInput()), sizeof(p.ip));
    strlcpy(p.accessCode, lv_textarea_get_text(setup_.codeInput()), sizeof(p.accessCode));
    if (printerIsKlipper(p)) {
        const char *portText = lv_textarea_get_text(setup_.portInput());
        const int port = portText && portText[0] ? atoi(portText) : 0;
        p.port = (port > 0 && port <= 65535) ? static_cast<uint16_t>(port) : 0;
        p.serial[0] = '\0';
    } else {
        strlcpy(p.serial, lv_textarea_get_text(setup_.serialInput()), sizeof(p.serial));
        p.port = 0;
    }
    if (!p.name[0]) strlcpy(p.name, p.ip[0] ? p.ip : printerTypeLabel(p.type), sizeof(p.name));
    saveConfig();
    BambuFleet::instance().reload();
    KlipperFleet::instance().reload();
    PaxxNotify::show("Printer", "Saved");
    showFleet();
}

void CupboardApp::startDiscovery() {
    discovery_.startScan(4500);
    PaxxNotify::show("Discover", "Searching LAN for Bambu printers...");
}

void CupboardApp::refreshPrinters() {
    BambuFleet::instance().requestRefresh();
    KlipperFleet::instance().requestRefresh();
    discovery_.startScan(4500);
    PaxxNotify::show("Farm", "Refreshing printers");
}

void CupboardApp::mergeDiscoveredPrinters() {
    bool changed = false;
    bool ipChanged = false;
    for (const auto &d : discovery_.results()) {
        for (int i = 0; i < config_.printerCount; ++i) {
            PrinterProfile &p = config_.printers[i];
            if (printerIsKlipper(p)) continue;
            const bool serialHit = d.serial[0] && p.serial[0] && strcmp(d.serial, p.serial) == 0;
            const bool ipHit = d.ip[0] && p.ip[0] && strcmp(d.ip, p.ip) == 0;
            if (!serialHit && !ipHit) continue;
            if (d.ip[0] && strcmp(p.ip, d.ip) != 0) {
                Serial.printf("[SSDP] %s IP %s -> %s\n", p.name, p.ip, d.ip);
                strlcpy(p.ip, d.ip, sizeof(p.ip));
                changed = true;
                ipChanged = true;
            }
            if (d.serial[0] && strcmp(p.serial, d.serial) != 0) {
                strlcpy(p.serial, d.serial, sizeof(p.serial));
                changed = true;
            }
            if (d.model[0] && !p.model[0]) {
                strlcpy(p.model, d.model, sizeof(p.model));
                changed = true;
            }
        }
    }
    if (!changed) return;
    saveConfig();
    if (ipChanged) BambuFleet::instance().reload();
}

void CupboardApp::loop() {
    pt_loop_display();
    applyPendingScreen();
    wifi_.loop();
    discovery_.loop();
    mergeDiscoveredPrinters();
    BambuFleet::instance().loop();
    KlipperFleet::instance().loop();
    PaxxNotify::loop();

    if (wifiLostAtMs_ != 0 && !WiFi.isConnected() && millis() - wifiLostAtMs_ > 5000) {
        wifiLostAtMs_ = 0;
    }

    if (activeTickKind_ && strcmp(activeTickKind_, "fleet") == 0) {
        fleet_.onTick();
    } else if (activeTickKind_ && strcmp(activeTickKind_, "detail") == 0) {
        detail_.onTick();
    }
    applyPendingScreen();
}

void CupboardApp::showGlobalLoading(bool visible, const char *text) {
    if (!globalLoadingOverlay_) return;
    paxx_set_loading_visible(globalLoadingArc_, globalLoadingLbl_, visible, text);
    if (visible) {
        lv_obj_remove_flag(globalLoadingOverlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(globalLoadingOverlay_);
        if (gearBtn_) lv_obj_move_foreground(gearBtn_);
    } else {
        lv_obj_add_flag(globalLoadingOverlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CupboardApp::refreshDisplay() {
    hideGearMenu();
    pt_refresh_display();
    if (shell_) lv_obj_invalidate(shell_);
    if (gearBtn_) lv_obj_move_foreground(gearBtn_);
}

void CupboardApp::onKeyboardVisibility(bool visible, void *userData) {
    auto *app = static_cast<CupboardApp *>(userData);
    if (!app) return;
    if (visible) {
        app->hideGearMenu();
        if (app->gearBtn_) lv_obj_add_flag(app->gearBtn_, LV_OBJ_FLAG_HIDDEN);
    } else if (app->gearBtn_) {
        lv_obj_remove_flag(app->gearBtn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(app->gearBtn_);
    }
}

void CupboardApp::hideGearMenu() {
    if (gearMenu_) lv_obj_add_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN);
}

void CupboardApp::toggleGearMenu() {
    if (!gearMenu_) return;
    if (lv_obj_has_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(gearMenu_);
        lv_obj_move_foreground(gearBtn_);
    } else {
        hideGearMenu();
    }
}

void CupboardApp::buildGearMenu() {
    gearBtn_ = lv_button_create(shell_);
    lv_obj_set_size(gearBtn_, 40, 40);
    lv_obj_align(gearBtn_, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_set_style_bg_color(gearBtn_, PaxxTheme::accent(), LV_PART_MAIN);
    paxx_mark_accent_fill(gearBtn_);
    lv_obj_add_event_cb(gearBtn_, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->toggleGearMenu();
    }, LV_EVENT_CLICKED, this);
    paxx_set_centered_icon(gearBtn_, LV_SYMBOL_SETTINGS);

    gearMenu_ = lv_obj_create(shell_);
    lv_obj_set_size(gearMenu_, 240, 390);
    lv_obj_align(gearMenu_, LV_ALIGN_TOP_RIGHT, -8, 52);
    lv_obj_set_style_bg_color(gearMenu_, PaxxTheme::surface(isDark()), LV_PART_MAIN);
    lv_obj_set_style_border_width(gearMenu_, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(gearMenu_, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(gearMenu_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gearMenu_, 6, LV_PART_MAIN);
    lv_obj_add_flag(gearMenu_, LV_OBJ_FLAG_HIDDEN);
    paxx_disable_input(gearMenu_);

    auto addItem = [&](const char *icon, const char *label, lv_event_cb_t cb) {
        lv_obj_t *btn = lv_button_create(gearMenu_);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 40);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
        lv_obj_set_layout(btn, LV_LAYOUT_NONE);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

        lv_obj_t *ico = lv_label_create(btn);
        lv_label_set_text(ico, icon);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 12, 0);
        paxx_mark_accent_text(ico);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    };

    addItem(LV_SYMBOL_HOME, "Farm", [](lv_event_t *e) {
        auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showFleet();
    });
    addItem(LV_SYMBOL_WIFI, "WiFi Setup", [](lv_event_t *e) {
        auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showWifi();
    });
    addItem(LV_SYMBOL_LIST, "Printers", [](lv_event_t *e) {
        auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showPrinterManager();
    });
    addItem(LV_SYMBOL_REFRESH, "Refresh Display", [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->refreshDisplay();
    });
    addItem(LV_SYMBOL_TINT, "Theme", [](lv_event_t *e) {
        auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showTheme();
    });
    addItem(LV_SYMBOL_IMAGE, "Display", [](lv_event_t *e) {
        auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showDisplay();
    });
    addItem(LV_SYMBOL_SETTINGS, "About", [](lv_event_t *e) {
        auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
        app->hideGearMenu();
        app->showSettings();
    });
    addItem(LV_SYMBOL_POWER, "Reboot", [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->hideGearMenu();
        ESP.restart();
    });
    lv_obj_move_foreground(gearBtn_);
    paxx_apply_accent_chrome(shell_);
}

void CupboardApp::buildShell() {
    shell_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(shell_, PaxxTheme::bg(isDark()), LV_PART_MAIN);
    paxx_disable_input(shell_);

    content_ = lv_obj_create(shell_);
    lv_obj_set_size(content_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content_, 0, LV_PART_MAIN);
    paxx_disable_input(content_);

    fleet_.create(this, content_);
    detail_.create(this, content_);
    typeSelect_.create(this, content_);
    setup_.create(this, content_);
    printerManager_.create(this, content_);
    wifiScreen_.create(this, content_);
    themeScreen_.create(this, content_);
    displayScreen_.create(this, content_);
    settings_.create(this, content_);

    lv_screen_load(shell_);

    globalLoadingOverlay_ = lv_obj_create(shell_);
    lv_obj_set_size(globalLoadingOverlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(globalLoadingOverlay_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(globalLoadingOverlay_, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_width(globalLoadingOverlay_, 0, LV_PART_MAIN);
    lv_obj_add_flag(globalLoadingOverlay_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(globalLoadingOverlay_, LV_OBJ_FLAG_HIDDEN);
    paxx_disable_input(globalLoadingOverlay_);
    globalLoadingArc_ = paxx_create_loading_arc(globalLoadingOverlay_);
    globalLoadingLbl_ = lv_label_create(globalLoadingOverlay_);
    lv_obj_align(globalLoadingLbl_, LV_ALIGN_CENTER, 0, 32);
    lv_obj_set_width(globalLoadingLbl_, kPaxxFormWidth);
    lv_label_set_long_mode(globalLoadingLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(globalLoadingLbl_, LV_OBJ_FLAG_HIDDEN);

    PaxxKeyboard::init(shell_);
    PaxxKeyboard::setVisibilityListener(onKeyboardVisibility, this);
    buildGearMenu();
}

void CupboardApp::showScreen(lv_obj_t *screen, const char *tickKind) {
    pendingScreen_ = screen;
    pendingTickKind_ = tickKind;
}

void CupboardApp::applyPendingScreen() {
    if (!pendingScreen_) return;
    lv_obj_t *screen = pendingScreen_;
    const char *tickKind = pendingTickKind_;
    pendingScreen_ = nullptr;
    pendingTickKind_ = nullptr;
    presentScreen(screen, tickKind);
}

void CupboardApp::presentScreen(lv_obj_t *screen, const char *tickKind) {
    if (activeScreen_ == displayScreen_.root() && screen != displayScreen_.root()) {
        displayScreen_.commit();
    }
    showGlobalLoading(false);
    hideGearMenu();
    PaxxKeyboard::hide();

    lv_obj_add_flag(fleet_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detail_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(typeSelect_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(setup_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(printerManager_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifiScreen_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(themeScreen_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(displayScreen_.root(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_.root(), LV_OBJ_FLAG_HIDDEN);

    activeScreen_ = screen;
    activeTickKind_ = tickKind;
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_HIDDEN);

    if (screen == fleet_.root()) fleet_.onEnter();
    else if (screen == detail_.root()) detail_.onEnter();
    else if (screen == typeSelect_.root()) typeSelect_.onEnter();
    else if (screen == setup_.root()) setup_.onEnter();
    else if (screen == printerManager_.root()) printerManager_.onEnter();
    else if (screen == wifiScreen_.root()) wifiScreen_.onEnter();
    else if (screen == themeScreen_.root()) themeScreen_.onEnter();
    else if (screen == displayScreen_.root()) displayScreen_.onEnter();

    if (gearBtn_) lv_obj_move_foreground(gearBtn_);
    paxx_ui_refresh();
}

void CupboardApp::showFleet() {
    BambuFleet::instance().setFocus(-1);
    showScreen(fleet_.root(), "fleet");
}
void CupboardApp::showPrinter(int index) {
    if (index < 0 || index >= config_.printerCount) return;
    config_.editIndex = index;
    BambuFleet::instance().setFocus(index);
    showScreen(detail_.root(), "detail");
}
void CupboardApp::showPrinterType() { showScreen(typeSelect_.root()); }
void CupboardApp::showSetup() { showScreen(setup_.root()); }
void CupboardApp::showPrinterManager() { showScreen(printerManager_.root()); }
void CupboardApp::showWifi() { showScreen(wifiScreen_.root()); }
void CupboardApp::showTheme() { showScreen(themeScreen_.root()); }
void CupboardApp::showDisplay() { showScreen(displayScreen_.root()); }
void CupboardApp::showSettings() { showScreen(settings_.root()); }

void farm_back_fleet_cb(lv_event_t *e) {
    static_cast<CupboardApp *>(lv_event_get_user_data(e))->showFleet();
}
