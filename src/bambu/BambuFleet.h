#pragma once

#include "bambu/BambuDiscovery.h"
#include "bambu/BambuTypes.h"
#include "storage/Preferences.h"

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class BambuFleet {
public:
    static BambuFleet &instance();

    void begin(AppConfig *cfg, BambuDiscovery *discovery = nullptr);
    void loop();
    void reload();
    void forgetPrinter(int index);
    void requestImmediate();

    int count() const;
    bool getPrinter(int index, PrinterProfile *profile, PrinterLive *live) const;
    void snapshot(PrinterProfile *profiles, PrinterLive *live, int *count) const;
    void sortedIndexes(int *outIdx, int *outCount, FleetSort sort) const;
    void setFocus(int index);
    int focus() const { return focusIndex_; }
    bool sendPrintCommand(int index, const char *command);
    bool reprintLast(int index);
    void applyExternalLive(int index, const PrinterLive &live);

private:
    BambuFleet() = default;
    static void taskThunk(void *arg);
    static void mqttThunk(char *topic, uint8_t *payload, unsigned int len);

    void taskLoop();
    void disconnectMqtt();
    bool connectCurrent();
    void handlePayload(const char *topic, const uint8_t *payload, unsigned int len);
    void nextPrinter();
    void applyPrintJson(int index, const char *gcodeState, int percent, int remainMin, const char *task);
    void persistPendingSerial();
    bool serialFromTls(char *out, size_t outLen);
    void requestPushAll(const char *serial);
    void flushPendingCommand();
    void publishPrintCommand(const char *serial, const char *command, const char *param);

    bool connectMqtt(const char *ipStr, const char *accessCode, const char *name);
    int nextConfigured(int from) const;
    bool printerReady(int index) const;
    bool hasReadyPrinter() const;
    bool inBackoff(int index) const;
    void noteConnectResult(int index, bool ok);

    AppConfig *cfg_ = nullptr;
    BambuDiscovery *discovery_ = nullptr;
    PrinterLive live_[BAMBU_MAX_PRINTERS]{};
    WiFiClientSecure *tls_ = nullptr;
    PubSubClient *mqtt_ = nullptr;
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    volatile bool reloadRequested_ = false;
    volatile bool serialDirty_ = false;
    int pollIndex_ = 0;
    uint32_t connectStartMs_ = 0;
    uint32_t lastStatusMs_ = 0;
    uint32_t wifiReadyMs_ = 0;
    uint32_t lastPushMs_ = 0;
    uint32_t seq_ = 1;
    volatile int focusIndex_ = -1;
    volatile int pendingIndex_ = -1;
    char pendingCmd_[24] = {};
    char pendingParam_[80] = {};
    bool gotStatus_ = false;
    bool started_ = false;
    bool sessionOpen_ = false;
    volatile bool forceDisconnect_ = false;
    char sessionIp_[16] = {};
    uint32_t skipUntilMs_[BAMBU_MAX_PRINTERS] = {};
    uint8_t failStreak_[BAMBU_MAX_PRINTERS] = {};
};
