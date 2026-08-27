#pragma once

#include "bambu/BambuTypes.h"
#include "storage/Preferences.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class KlipperFleet {
public:
    static KlipperFleet &instance();

    void begin(AppConfig *cfg);
    void loop();
    void reload();
    void forgetPrinter(int index);
    void setPaused(bool paused);
    bool isPauseIdle() const { return !paused_ || pauseIdle_; }
    void requestRefresh();
    bool sendPrintCommand(int index, const char *command);
    bool reprintLast(int index);

private:
    KlipperFleet() = default;
    static void taskThunk(void *arg);

    void taskLoop();
    bool printerReady(int index) const;
    bool inBackoff(int index) const;
    void noteConnectResult(int index, bool ok);
    int nextConfigured(int from) const;
    int nextRefreshIndex() const;
    void noteRefreshVisited(int index);
    bool hasReadyPrinter() const;
    bool pollPrinter(int index);
    bool postCommand(int index, const char *path, const char *body);
    void flushPendingCommand();

    AppConfig *cfg_ = nullptr;
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool started_ = false;
    volatile bool reloadRequested_ = false;
    volatile bool paused_ = false;
    volatile bool pauseIdle_ = false;
    int pollIndex_ = 0;
    uint32_t skipUntilMs_[BAMBU_MAX_PRINTERS] = {};
    uint8_t failStreak_[BAMBU_MAX_PRINTERS] = {};
    volatile bool refreshSweep_ = false;
    uint32_t refreshVisited_ = 0;
    volatile int pendingIndex_ = -1;
    char pendingCmd_[24] = {};
    char pendingParam_[80] = {};
};
