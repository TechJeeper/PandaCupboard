#include "net/WifiService.h"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

namespace {
constexpr unsigned long kPauseWaitMs = 9000;
constexpr unsigned long kDropWaitMs = 800;
constexpr unsigned long kScanPassTimeoutMs = 10000;
constexpr unsigned long kScanGapMs = 350;
constexpr int kScanMaxStartAttempts = 3;
constexpr int kScanPasses = 2;
}

void WifiService::setScanLifecycle(WifiVoidCallback pause, WifiVoidCallback resume, WifiReadyCallback idle) {
    pauseCb_ = std::move(pause);
    resumeCb_ = std::move(resume);
    idleCb_ = std::move(idle);
}

void WifiService::prepareStation() {
    // Keep STA config in RAM during connect/scan so each begin() does not rewrite NVS.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);
}

bool WifiService::startConnect(const char *ssid, const char *password, int timeoutSec) {
    if (!ssid || !ssid[0]) return false;

    abortScan();

    strlcpy(lastSsid_, ssid, sizeof(lastSsid_));
    if (password) strlcpy(lastPass_, password, sizeof(lastPass_));
    else lastPass_[0] = '\0';

    Serial.printf("[WiFi] connect ssid=\"%s\" timeout=%ds\n", ssid, timeoutSec);
    prepareStation();
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    delay(30);
    WiFi.begin(ssid, password);

    connectPending_ = true;
    connectStartMs_ = millis();
    connectTimeoutSec_ = timeoutSec;
    return true;
}

void WifiService::finishConnect(bool ok, const char *message) {
    connectPending_ = false;
    lastConnected_ = ok && WiFi.status() == WL_CONNECTED;
    if (ok) {
        Serial.printf("[WiFi] connected ip=%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[WiFi] connect failed status=%d internal_free=%u\n",
                      static_cast<int>(WiFi.status()),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
    if (resumeAfterConnect_) {
        resumeAfterConnect_ = false;
        resumeClients();
    }
    if (statusCb_) statusCb_(ok, message);
}

bool WifiService::connect(const char *ssid, const char *password, int timeoutSec) {
    if (!startConnect(ssid, password, timeoutSec)) return false;

    const unsigned long deadline = millis() + static_cast<unsigned long>(timeoutSec) * 1000UL;
    while (millis() < deadline) {
        if (WiFi.status() == WL_CONNECTED) {
            finishConnect(true, localIp());
            return true;
        }
        delay(50);
        loop();
    }

    finishConnect(false, "WiFi connection failed");
    return false;
}

void WifiService::disconnect() {
    Serial.println("[WiFi] disconnect");
    connectPending_ = false;
    abortScan();
    WiFi.disconnect(true);
}

void WifiService::forgetAll() {
    Serial.println("[WiFi] forget all saved networks");
    connectPending_ = false;
    lastSsid_[0] = '\0';
    lastPass_[0] = '\0';
    abortScan();
    reconnectAfterScan_ = false;
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
}

const char *WifiService::localIp() const {
    static char buf[16];
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
    } else {
        buf[0] = '\0';
    }
    return buf;
}

void WifiService::rememberCurrentNetwork() {
    if (WiFi.status() == WL_CONNECTED) {
        const String ssid = WiFi.SSID();
        if (ssid.length()) strlcpy(lastSsid_, ssid.c_str(), sizeof(lastSsid_));
        const String psk = WiFi.psk();
        if (psk.length()) strlcpy(lastPass_, psk.c_str(), sizeof(lastPass_));
    }
}

void WifiService::pauseClients() {
    if (clientsPaused_) return;
    clientsPaused_ = true;
    if (pauseCb_) pauseCb_();
}

void WifiService::resumeClients() {
    if (!clientsPaused_) return;
    clientsPaused_ = false;
    if (resumeCb_) resumeCb_();
}

void WifiService::abortScan() {
    if (scanPhase_ == ScanPhase::Idle) return;
    Serial.println("[WiFi] abort scan");
    if (scanPhase_ == ScanPhase::Scanning) WiFi.scanDelete();
    scanPhase_ = ScanPhase::Idle;
    scanPass_ = 0;
    scanStartAttempts_ = 0;
    phaseUntilMs_ = 0;
    reconnectAfterScan_ = false;
    WiFi.setAutoReconnect(true);
    if (!resumeAfterConnect_) resumeClients();
}

void WifiService::mergeNetworks(std::vector<WifiNetwork> &into, const std::vector<WifiNetwork> &add) {
    for (const auto &net : add) {
        if (!net.ssid[0]) continue;
        bool found = false;
        for (auto &existing : into) {
            if (strcmp(existing.ssid, net.ssid) != 0) continue;
            if (net.rssi > existing.rssi) existing = net;
            found = true;
            break;
        }
        if (!found) into.push_back(net);
    }
    std::sort(into.begin(), into.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
        return a.rssi > b.rssi;
    });
}

void WifiService::collectScanResults(int n, std::vector<WifiNetwork> &out) {
    out.clear();
    if (n <= 0) return;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        WifiNetwork net{};
        strlcpy(net.ssid, WiFi.SSID(i).c_str(), sizeof(net.ssid));
        net.rssi = WiFi.RSSI(i);
        net.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        if (!net.ssid[0]) continue;
        out.push_back(net);
        Serial.printf("[WiFi]   [%d] \"%s\" rssi=%d secure=%d\n",
                      i, net.ssid, net.rssi, net.secure ? 1 : 0);
    }
}

void WifiService::takeScanResults() {
    const int n = WiFi.scanComplete();
    std::vector<WifiNetwork> pass;
    if (n > 0) collectScanResults(n, pass);
    WiFi.scanDelete();
    mergeNetworks(accumulated_, pass);
    Serial.printf("[WiFi] scan pass %u raw=%d merged=%u\n",
                  static_cast<unsigned>(scanPass_), n, static_cast<unsigned>(accumulated_.size()));
}

void WifiService::resumeStation() {
    if (!lastSsid_[0]) {
        resumeClients();
        return;
    }
    Serial.printf("[WiFi] restoring connection to \"%s\" after scan\n", lastSsid_);
    resumeAfterConnect_ = true;
    prepareStation();
    WiFi.setAutoReconnect(true);
    WiFi.begin(lastSsid_, lastPass_);
    connectPending_ = true;
    connectStartMs_ = millis();
    connectTimeoutSec_ = 15;
}

void WifiService::finishScanPasses() {
    scanPhase_ = ScanPhase::Idle;
    scanStartAttempts_ = 0;
    phaseUntilMs_ = 0;
    WiFi.setAutoReconnect(true);
    Serial.printf("[WiFi] scan finished, %u network(s)\n",
                  static_cast<unsigned>(accumulated_.size()));
    const bool reconnect = reconnectAfterScan_ && lastSsid_[0];
    reconnectAfterScan_ = false;
    if (scanCb_) scanCb_(accumulated_);
    if (reconnect) resumeStation();
    else resumeClients();
}

void WifiService::beginAsyncScan() {
    prepareStation();
    WiFi.setAutoReconnect(false);

    const int pending = WiFi.scanComplete();
    if (pending == WIFI_SCAN_RUNNING) {
        scanPhase_ = ScanPhase::Scanning;
        scanStartMs_ = millis();
        Serial.println("[WiFi] async scan already running");
        return;
    }
    if (pending >= 0) WiFi.scanDelete();

    Serial.printf("[WiFi] async scan start pass %u\n", static_cast<unsigned>(scanPass_ + 1));
    const int rc = WiFi.scanNetworks(true, false, false, 400);
    if (rc == WIFI_SCAN_FAILED) {
        scanStartAttempts_++;
        Serial.printf("[WiFi] async scan failed to start (attempt %u)\n",
                      static_cast<unsigned>(scanStartAttempts_));
        if (scanStartAttempts_ >= kScanMaxStartAttempts) {
            finishScanPasses();
            return;
        }
        scanPhase_ = ScanPhase::Gap;
        phaseUntilMs_ = millis() + kScanGapMs;
        return;
    }
    scanStartAttempts_ = 0;
    scanPass_++;
    scanPhase_ = ScanPhase::Scanning;
    scanStartMs_ = millis();
    if (rc >= 0) {
        takeScanResults();
        if (scanPass_ >= kScanPasses) finishScanPasses();
        else {
            scanPhase_ = ScanPhase::Gap;
            phaseUntilMs_ = millis() + kScanGapMs;
        }
    }
}

void WifiService::startScan() {
    if (scanPhase_ != ScanPhase::Idle) {
        Serial.println("[WiFi] scan already in progress");
        return;
    }

    rememberCurrentNetwork();
    if (connectPending_) {
        Serial.println("[WiFi] cancelling connect so scan can run");
        connectPending_ = false;
    }

    accumulated_.clear();
    scanPass_ = 0;
    scanStartAttempts_ = 0;
    reconnectAfterScan_ = lastSsid_[0] != '\0';
    resumeAfterConnect_ = false;

    pauseClients();
    scanPhase_ = ScanPhase::Pausing;
    phaseUntilMs_ = millis() + kPauseWaitMs;
    Serial.println("[WiFi] scan pausing printer links");
}

void WifiService::loop() {
    const unsigned long now = millis();

    if (scanPhase_ == ScanPhase::Pausing) {
        const bool idle = !idleCb_ || idleCb_();
        if (idle || now >= phaseUntilMs_) {
            Serial.printf("[WiFi] leaving AP for scan (clients_idle=%d)\n", idle ? 1 : 0);
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, false);
            scanPhase_ = ScanPhase::Dropping;
            phaseUntilMs_ = now + kDropWaitMs;
        }
    } else if (scanPhase_ == ScanPhase::Dropping) {
        if (WiFi.status() != WL_CONNECTED || now >= phaseUntilMs_) {
            beginAsyncScan();
        }
    } else if (scanPhase_ == ScanPhase::Gap) {
        if (now >= phaseUntilMs_) beginAsyncScan();
    } else if (scanPhase_ == ScanPhase::Scanning) {
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            if (now - scanStartMs_ > kScanPassTimeoutMs) {
                Serial.println("[WiFi] async scan timeout");
                WiFi.scanDelete();
                if (scanPass_ >= kScanPasses) finishScanPasses();
                else {
                    scanPhase_ = ScanPhase::Gap;
                    phaseUntilMs_ = now + kScanGapMs;
                }
            }
        } else {
            takeScanResults();
            if (scanPass_ >= kScanPasses) finishScanPasses();
            else {
                scanPhase_ = ScanPhase::Gap;
                phaseUntilMs_ = now + kScanGapMs;
            }
        }
    }

    if (connectPending_ && scanPhase_ == ScanPhase::Idle) {
        if (WiFi.status() == WL_CONNECTED) {
            finishConnect(true, localIp());
        } else if (now - connectStartMs_ > static_cast<unsigned long>(connectTimeoutSec_) * 1000UL) {
            finishConnect(false, "WiFi connection failed");
        }
    }

    const bool connected = isConnected();
    if (connected) {
        WiFi.setSleep(WIFI_PS_NONE);
    }
    if (connected != lastConnected_) {
        lastConnected_ = connected;
        if (statusCb_) {
            statusCb_(connected, connected ? localIp() : "WiFi disconnected");
        }
    }
}
