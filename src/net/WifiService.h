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

class WifiService {
public:
    bool connect(const char *ssid, const char *password, int timeoutSec = 20);
    bool startConnect(const char *ssid, const char *password, int timeoutSec = 20);
    bool isConnectPending() const { return connectPending_; }
    void disconnect();
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    const char *localIp() const;
    void startScan();
    bool isScanning() const { return scanRunning_; }
    bool isReconnectAfterScan() const { return reconnectAfterScan_; }
    void forgetAll();
    void setStatusCallback(WifiStatusCallback cb) { statusCb_ = std::move(cb); }
    void setScanCallback(WifiScanCallback cb) { scanCb_ = std::move(cb); }
    void loop();

private:
    void finishConnect(bool ok, const char *message);
    void abortScan();
    void beginAsyncScan();
    void finishScan(int n);
    void rememberCurrentNetwork();
    static void collectScanResults(int n, std::vector<WifiNetwork> &out);

    WifiStatusCallback statusCb_;
    WifiScanCallback scanCb_;
    bool lastConnected_ = false;
    bool connectPending_ = false;
    bool scanRunning_ = false;
    bool reconnectAfterScan_ = false;
    unsigned long connectStartMs_ = 0;
    unsigned long scanStartMs_ = 0;
    unsigned long scanArmAtMs_ = 0;
    int connectTimeoutSec_ = 0;
    uint8_t scanStartAttempts_ = 0;
    char lastSsid_[33] = {};
    char lastPass_[64] = {};
};
