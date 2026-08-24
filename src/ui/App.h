#pragma once

#include "bambu/BambuDiscovery.h"
#include "bambu/BambuFleet.h"
#include "net/WifiService.h"
#include "storage/Preferences.h"

#include <lvgl.h>
#include <vector>

class CupboardApp;

class FleetScreen {
public:
    void create(CupboardApp *app, lv_obj_t *parent);
    void onEnter();
    void onTick();
    lv_obj_t *root() const { return screen_; }

private:
    void rebuild();
    static void onSortName(lv_event_t *e);
    static void onSortStatus(lv_event_t *e);

    CupboardApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *nameHdr_ = nullptr;
    lv_obj_t *statusHdr_ = nullptr;
    uint32_t lastUiMs_ = 0;
    PrinterProfile profiles_[BAMBU_MAX_PRINTERS]{};
    PrinterLive live_[BAMBU_MAX_PRINTERS]{};
    int order_[BAMBU_MAX_PRINTERS]{};
};

class DetailScreen {
public:
    void create(CupboardApp *app, lv_obj_t *parent);
    void onEnter();
    void onTick();
    lv_obj_t *root() const { return screen_; }

private:
    void refresh();
    void setStopConfirm(bool visible);
    static void onPauseResume(lv_event_t *e);
    static void onStop(lv_event_t *e);
    static void onReprint(lv_event_t *e);
    static void onEdit(lv_event_t *e);

    CupboardApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *nameLbl_ = nullptr;
    lv_obj_t *taskLbl_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    lv_obj_t *percentLbl_ = nullptr;
    lv_obj_t *remainLbl_ = nullptr;
    lv_obj_t *layerLbl_ = nullptr;
    lv_obj_t *tempLbl_ = nullptr;
    lv_obj_t *metaLbl_ = nullptr;
    lv_obj_t *bar_ = nullptr;
    lv_obj_t *pauseBtn_ = nullptr;
    lv_obj_t *pauseLbl_ = nullptr;
    lv_obj_t *stopBtn_ = nullptr;
    lv_obj_t *reprintBtn_ = nullptr;
    lv_obj_t *confirm_ = nullptr;
    uint32_t lastUiMs_ = 0;
};

class SetupScreen {
public:
    void create(CupboardApp *app, lv_obj_t *parent);
    void onEnter();
    void loadFromEdit();
    lv_obj_t *root() const { return screen_; }
    lv_obj_t *nameInput() const { return nameTa_; }
    lv_obj_t *ipInput() const { return ipTa_; }
    lv_obj_t *codeInput() const { return codeTa_; }
    lv_obj_t *serialInput() const { return serialTa_; }

    void applyDiscovery(const DiscoveredPrinter &d);
    void refreshDiscoverList();

private:
    CupboardApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *nameTa_ = nullptr;
    lv_obj_t *ipTa_ = nullptr;
    lv_obj_t *codeTa_ = nullptr;
    lv_obj_t *serialTa_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
    lv_obj_t *discoverList_ = nullptr;
};

class PrinterManagerScreen {
public:
    void create(CupboardApp *app, lv_obj_t *parent);
    void onEnter();
    void rebuildList();
    lv_obj_t *root() const { return screen_; }

private:
    struct RowCtx {
        CupboardApp *app = nullptr;
        int index = -1;
    };
    CupboardApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
    std::vector<RowCtx> rowCtxs_;
};

class WifiScreen {
public:
    void create(CupboardApp *app, lv_obj_t *parent);
    void onEnter();
    void scanNetworks();
    void forgetAllNetworks();
    void connectSelected();
    void setStatus(const char *text);
    bool isScanning() const { return scanning_; }
    lv_obj_t *passInput() const { return passTa_; }
    lv_obj_t *root() const { return screen_; }

private:
    void applyNetworkList(const std::vector<WifiNetwork> &nets);
    void selectNetwork(size_t index);
    void updateNavBack();

    CupboardApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *navBackBtn_ = nullptr;
    lv_obj_t *networkList_ = nullptr;
    lv_obj_t *passTa_ = nullptr;
    lv_obj_t *statusLbl_ = nullptr;
    std::vector<WifiNetwork> networks_;
    int selectedIndex_ = -1;
    bool scanning_ = false;
};

class SettingsScreen {
public:
    void create(CupboardApp *app, lv_obj_t *parent);
    void setHint(const char *text);
    lv_obj_t *root() const { return screen_; }

private:
    CupboardApp *app_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *hintLbl_ = nullptr;
};

class CupboardApp {
public:
    void begin();
    void loop();

    WifiService &wifi() { return wifi_; }
    BambuDiscovery &discovery() { return discovery_; }
    AppConfig &config() { return config_; }
    bool isDark() const { return config_.darkTheme; }

    void saveConfig();
    bool addPrinter();
    bool removePrinter(int index);
    void editPrinter(int index);
    void savePrinterFromSetup();
    void startDiscovery();
    void mergeDiscoveredPrinters();

    void showFleet();
    void showPrinter(int index);
    void showSetup();
    void showPrinterManager();
    void showWifi();
    void showSettings();
    void showGlobalLoading(bool visible, const char *text = nullptr);
    void refreshDisplay();

    SetupScreen &setup() { return setup_; }
    PrinterManagerScreen &printerManager() { return printerManager_; }
    WifiScreen &wifiScreen() { return wifiScreen_; }
    SettingsScreen &settings() { return settings_; }
    FleetScreen &fleet() { return fleet_; }
    DetailScreen &detail() { return detail_; }

private:
    void buildShell();
    void buildGearMenu();
    void hideGearMenu();
    void toggleGearMenu();
    void showScreen(lv_obj_t *screen, const char *tickKind = nullptr);
    void presentScreen(lv_obj_t *screen, const char *tickKind);
    void applyPendingScreen();
    static void onKeyboardVisibility(bool visible, void *userData);

    AppConfig config_{};
    WifiService wifi_;
    BambuDiscovery discovery_;

    lv_obj_t *shell_ = nullptr;
    lv_obj_t *content_ = nullptr;
    lv_obj_t *gearBtn_ = nullptr;
    lv_obj_t *gearMenu_ = nullptr;
    lv_obj_t *globalLoadingOverlay_ = nullptr;
    lv_obj_t *globalLoadingArc_ = nullptr;
    lv_obj_t *globalLoadingLbl_ = nullptr;
    lv_obj_t *activeScreen_ = nullptr;
    lv_obj_t *pendingScreen_ = nullptr;
    const char *activeTickKind_ = nullptr;
    const char *pendingTickKind_ = nullptr;
    unsigned long wifiLostAtMs_ = 0;

    FleetScreen fleet_;
    DetailScreen detail_;
    SetupScreen setup_;
    PrinterManagerScreen printerManager_;
    WifiScreen wifiScreen_;
    SettingsScreen settings_;
};

void farm_back_fleet_cb(lv_event_t *e);
