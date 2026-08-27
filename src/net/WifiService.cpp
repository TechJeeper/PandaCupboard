#include "net/WifiService.h"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>

bool WifiService::startConnect(const char *ssid, const char *password, int timeoutSec) {
    if (!ssid || !ssid[0]) return false;

    abortScan();

    strlcpy(lastSsid_, ssid, sizeof(lastSsid_));
    if (password) strlcpy(lastPass_, password, sizeof(lastPass_));
    else lastPass_[0] = '\0';

    Serial.printf("[WiFi] connect ssid=\"%s\" timeout=%ds\n", ssid, timeoutSec);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.disconnect(true);
    delay(50);
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
    WiFi.disconnect(true);
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

void WifiService::abortScan() {
    if (!scanRunning_ && scanArmAtMs_ == 0) return;
    Serial.println("[WiFi] abort scan");
    scanArmAtMs_ = 0;
    scanStartAttempts_ = 0;
    if (scanRunning_) WiFi.scanDelete();
    scanRunning_ = false;
    WiFi.setAutoReconnect(true);
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
    std::sort(out.begin(), out.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
        return a.rssi > b.rssi;
    });
    std::vector<WifiNetwork> uniq;
    uniq.reserve(out.size());
    for (const auto &net : out) {
        bool seen = false;
        for (const auto &u : uniq) {
            if (strcmp(u.ssid, net.ssid) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) uniq.push_back(net);
    }
    out.swap(uniq);
}

void WifiService::finishScan(int n) {
    std::vector<WifiNetwork> nets;
    if (n > 0) collectScanResults(n, nets);
    WiFi.scanDelete();
    scanRunning_ = false;
    scanArmAtMs_ = 0;
    scanStartAttempts_ = 0;
    Serial.printf("[WiFi] async scan done, %u network(s) (raw=%d)\n",
                  static_cast<unsigned>(nets.size()), n);

    const bool reconnect = reconnectAfterScan_ && lastSsid_[0];
    reconnectAfterScan_ = false;
    WiFi.setAutoReconnect(true);

    if (scanCb_) scanCb_(nets);

    if (reconnect) {
        Serial.printf("[WiFi] restoring connection to \"%s\" after scan\n", lastSsid_);
        startConnect(lastSsid_, lastPass_, 15);
    }
}

void WifiService::beginAsyncScan() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setAutoReconnect(false);

    const int pending = WiFi.scanComplete();
    if (pending == WIFI_SCAN_RUNNING) {
        scanRunning_ = true;
        scanStartMs_ = millis();
        Serial.println("[WiFi] async scan already running");
        return;
    }
    if (pending >= 0) WiFi.scanDelete();

    Serial.println("[WiFi] async scan start");
    // Active scan, 300 ms/channel, non-blocking so LVGL / WDT keep running.
    const int rc = WiFi.scanNetworks(true, true, false, 300);
    if (rc == WIFI_SCAN_FAILED) {
        scanStartAttempts_++;
        Serial.printf("[WiFi] async scan failed to start (attempt %u)\n",
                      static_cast<unsigned>(scanStartAttempts_));
        if (scanStartAttempts_ >= 3) {
            finishScan(WIFI_SCAN_FAILED);
            return;
        }
        scanArmAtMs_ = millis() + 400;
        return;
    }
    scanStartAttempts_ = 0;
    scanRunning_ = true;
    scanStartMs_ = millis();
    if (rc >= 0) finishScan(rc);
}

void WifiService::startScan() {
    if (scanRunning_ || scanArmAtMs_ != 0) {
        Serial.println("[WiFi] scan already in progress");
        return;
    }

    rememberCurrentNetwork();
    if (connectPending_) {
        Serial.println("[WiFi] cancelling connect so scan can run");
        connectPending_ = false;
        WiFi.disconnect(false, false);
    }

    scanRunning_ = true;
    scanStartMs_ = millis();
    scanStartAttempts_ = 0;
    reconnectAfterScan_ = lastSsid_[0] != '\0';

    const bool associated = WiFi.status() == WL_CONNECTED;
    if (associated || reconnectAfterScan_) {
        Serial.println("[WiFi] leaving AP for scan");
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        scanArmAtMs_ = millis() + 400;
        return;
    }

    beginAsyncScan();
}

void WifiService::loop() {
    if (scanArmAtMs_ != 0) {
        if (millis() >= scanArmAtMs_) {
            scanArmAtMs_ = 0;
            beginAsyncScan();
        }
    } else if (scanRunning_) {
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            if (millis() - scanStartMs_ > 12000UL) {
                Serial.println("[WiFi] async scan timeout");
                finishScan(WIFI_SCAN_FAILED);
            }
        } else {
            finishScan(n);
        }
    }

    if (connectPending_ && !scanRunning_ && scanArmAtMs_ == 0) {
        if (WiFi.status() == WL_CONNECTED) {
            finishConnect(true, localIp());
        } else if (millis() - connectStartMs_ > static_cast<unsigned long>(connectTimeoutSec_) * 1000UL) {
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
