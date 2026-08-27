#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include <vector>

struct WifiNetwork {
    char ssid[33];
    int32_t rssi;
    bool secure;
};

using WifiStatusCallback = std::function<void(bool connected, const char *message)>;
using WifiScanCallback = std::function<void(const std::vector<WifiNetwork> &)>;
using WifiVoidCallback = std::function<void()>;
using WifiReadyCallback = std::function<bool()>;

class WifiService {
public:
    bool connect(const char *ssid, const char *password, int timeoutSec = 20);
    bool startConnect(const char *ssid, const char *password, int timeoutSec = 20);
    bool isConnectPending() const { return connectPending_; }
    void disconnect();
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    const char *localIp() const;
    void startScan();
    bool isScanning() const { return scanPhase_ != ScanPhase::Idle; }
    void forgetAll();
    void setStatusCallback(WifiStatusCallback cb) { statusCb_ = std::move(cb); }
    void setScanCallback(WifiScanCallback cb) { scanCb_ = std::move(cb); }
    void setScanLifecycle(WifiVoidCallback pause, WifiVoidCallback resume, WifiReadyCallback idle);
    void loop();

private:
    enum class ScanPhase : uint8_t { Idle, Pausing, Dropping, Scanning, Gap };

    void finishConnect(bool ok, const char *message);
    void abortScan();
    void pauseClients();
    void resumeClients();
    void beginAsyncScan();
    void takeScanResults();
    void finishScanPasses();
    void resumeStation();
    void rememberCurrentNetwork();
    static void prepareStation();
    static void collectScanResults(int n, std::vector<WifiNetwork> &out);
    static void mergeNetworks(std::vector<WifiNetwork> &into, const std::vector<WifiNetwork> &add);

    WifiStatusCallback statusCb_;
    WifiScanCallback scanCb_;
    WifiVoidCallback pauseCb_;
    WifiVoidCallback resumeCb_;
    WifiReadyCallback idleCb_;
    std::vector<WifiNetwork> accumulated_;
    bool lastConnected_ = false;
    bool connectPending_ = false;
    bool reconnectAfterScan_ = false;
    bool resumeAfterConnect_ = false;
    bool clientsPaused_ = false;
    ScanPhase scanPhase_ = ScanPhase::Idle;
    unsigned long connectStartMs_ = 0;
    unsigned long scanStartMs_ = 0;
    unsigned long phaseUntilMs_ = 0;
    int connectTimeoutSec_ = 0;
    uint8_t scanStartAttempts_ = 0;
    uint8_t scanPass_ = 0;
    char lastSsid_[33] = {};
    char lastPass_[65] = {};
};
