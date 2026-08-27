#include "bambu/BambuFleet.h"
#include "klipper/KlipperFleet.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>
#include <mbedtls/oid.h>
#include <mbedtls/x509_crt.h>

namespace {
BambuFleet *gFleet = nullptr;

const char *mqttFailReason(int rc) {
    if (rc == 4 || rc == 5) return "Bad access code";
    if (rc == -4) return "MQTT timeout";
    if (rc == -2) return "Wrong IP / MQTT closed";
    return "MQTT unreachable";
}

int statusRank(BambuGcodeState s) {
    if (bambuStateIsActive(s)) return 0;
    switch (s) {
        case BambuGcodeState::Finish: return 1;
        case BambuGcodeState::Failed:
        case BambuGcodeState::Stopped: return 2;
        case BambuGcodeState::Idle: return 3;
        case BambuGcodeState::Syncing: return 4;
        default: return 5;
    }
}

const char *taskFromJson(JsonObject print) {
    const char *n = print["subtask_name"] | "";
    if (n && n[0]) return n;
    n = print["gcode_file"] | "";
    if (n && n[0]) return n;
    n = print["mc_print_stage"] | "";
    return n ? n : "";
}

bool wifiReady() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}

constexpr uint32_t kIdlePollMs = 25000;

bool dueForPoll(const PrinterLive &live, uint32_t now) {
    if (!live.lastSeenMs) return true;
    if (bambuStateIsActive(live.state)) return true;
    return (now - live.lastSeenMs) >= kIdlePollMs;
}

void clipLog(char *dst, size_t dstLen, const uint8_t *src, unsigned int len) {
    if (!dst || dstLen == 0) return;
    const size_t n = len < dstLen - 1 ? len : dstLen - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    for (size_t i = 0; i < n; ++i) {
        if (dst[i] < 32 || dst[i] > 126) dst[i] = '.';
    }
}
}  // namespace

BambuFleet &BambuFleet::instance() {
    static BambuFleet inst;
    return inst;
}

void BambuFleet::mqttThunk(char *topic, uint8_t *payload, unsigned int len) {
    if (gFleet) gFleet->handlePayload(topic, payload, len);
}

void BambuFleet::taskThunk(void *arg) {
    static_cast<BambuFleet *>(arg)->taskLoop();
}

void BambuFleet::begin(AppConfig *cfg, BambuDiscovery *discovery) {
    cfg_ = cfg;
    discovery_ = discovery;
    gFleet = this;
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!started_) {
        started_ = true;
        // Core 1 is the Arduino loop core. Core 0 runs the WiFi/LwIP stack;
        // TLS + MQTT there races WiFi connect and trips the task watchdog.
        const BaseType_t core = 1;
        xTaskCreatePinnedToCore(taskThunk, "bambuMqtt", 24576, this, 1, &task_, core);
    }
}

void BambuFleet::reload() {
    if (pollIndex_ >= count()) forceDisconnect_ = true;
    reloadRequested_ = true;
}

void BambuFleet::forgetPrinter(int index) {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = index; i < BAMBU_MAX_PRINTERS - 1; ++i) live_[i] = live_[i + 1];
        live_[BAMBU_MAX_PRINTERS - 1] = PrinterLive{};
        if (pollIndex_ == index) forceDisconnect_ = true;
        else if (pollIndex_ > index) pollIndex_ -= 1;
        for (int i = index; i < BAMBU_MAX_PRINTERS - 1; ++i) {
            skipUntilMs_[i] = skipUntilMs_[i + 1];
            failStreak_[i] = failStreak_[i + 1];
        }
        skipUntilMs_[BAMBU_MAX_PRINTERS - 1] = 0;
        failStreak_[BAMBU_MAX_PRINTERS - 1] = 0;
        xSemaphoreGive(lock_);
    }
    reloadRequested_ = true;
}

void BambuFleet::requestImmediate() {
    gotStatus_ = true;
}

void BambuFleet::requestRefresh() {
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < BAMBU_MAX_PRINTERS; ++i) {
            skipUntilMs_[i] = 0;
            failStreak_[i] = 0;
        }
        xSemaphoreGive(lock_);
    }
    refreshVisited_ = 0;
    refreshSweep_ = true;
    forceDisconnect_ = true;
    reloadRequested_ = true;
    const int start = nextRefreshIndex();
    if (start >= 0) pollIndex_ = start;
    Serial.printf("[Bambu] refresh sweep start=%d\n", pollIndex_);
}

int BambuFleet::nextRefreshIndex() const {
    const int n = count();
    int firstOther = -1;
    for (int i = 0; i < n; ++i) {
        if (!printerReady(i)) continue;
        if (refreshVisited_ & (1u << i)) continue;
        if (bambuStateIsActive(live_[i].state)) return i;
        if (firstOther < 0) firstOther = i;
    }
    return firstOther;
}

void BambuFleet::noteRefreshVisited(int index) {
    if (!refreshSweep_ || index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    refreshVisited_ |= (1u << index);
    if (nextRefreshIndex() < 0) refreshSweep_ = false;
}

void BambuFleet::setPaused(bool paused) {
    paused_ = paused;
    pauseIdle_ = false;
    if (paused) forceDisconnect_ = true;
}

int BambuFleet::count() const {
    return cfg_ ? cfg_->printerCount : 0;
}

bool BambuFleet::getPrinter(int index, PrinterProfile *profile, PrinterLive *live) const {
    if (!cfg_ || !profile || !live || index < 0 || index >= cfg_->printerCount) return false;
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    *profile = cfg_->printers[index];
    *live = live_[index];
    if (!live->online && live->lastSeenMs == 0 && live->state != BambuGcodeState::Syncing) {
        live->state = BambuGcodeState::Offline;
    }
    xSemaphoreGive(lock_);
    return true;
}

void BambuFleet::snapshot(PrinterProfile *profiles, PrinterLive *live, int *count) const {
    if (!cfg_ || !profiles || !live || !count) return;
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) != pdTRUE) {
        *count = 0;
        return;
    }
    const int n = cfg_->printerCount;
    *count = n;
    for (int i = 0; i < n; ++i) {
        profiles[i] = cfg_->printers[i];
        live[i] = live_[i];
        if (!live[i].online && live[i].lastSeenMs == 0 && live[i].state != BambuGcodeState::Syncing) {
            live[i].state = BambuGcodeState::Offline;
        }
    }
    xSemaphoreGive(lock_);
}

void BambuFleet::sortedIndexes(int *outIdx, int *outCount, FleetSort sort) const {
    int n = 0;
    PrinterProfile *profiles = new PrinterProfile[BAMBU_MAX_PRINTERS];
    PrinterLive *live = new PrinterLive[BAMBU_MAX_PRINTERS];
    if (!profiles || !live) {
        delete[] profiles;
        delete[] live;
        *outCount = 0;
        return;
    }
    snapshot(profiles, live, &n);
    *outCount = n;
    for (int i = 0; i < n; ++i) outIdx[i] = i;

    auto better = [&](int a, int b) {
        const PrinterLive &la = live[a];
        const PrinterLive &lb = live[b];
        if (sort == FleetSort::DeviceName) {
            return strcasecmp(profiles[a].name, profiles[b].name) < 0;
        }
        const int ra = statusRank(la.state);
        const int rb = statusRank(lb.state);
        if (ra != rb) return ra < rb;
        if (bambuStateIsActive(la.state) && bambuStateIsActive(lb.state)) {
            const int pa = la.percent < 0 ? 0 : la.percent;
            const int pb = lb.percent < 0 ? 0 : lb.percent;
            if (pa != pb) return pa > pb;
        }
        return strcasecmp(profiles[a].name, profiles[b].name) < 0;
    };

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (better(outIdx[j], outIdx[i])) {
                const int tmp = outIdx[i];
                outIdx[i] = outIdx[j];
                outIdx[j] = tmp;
            }
        }
    }
    delete[] profiles;
    delete[] live;
}

void BambuFleet::disconnectMqtt() {
    sessionIp_[0] = '\0';
    if (!mqtt_ || !tls_) {
        sessionOpen_ = false;
        gotStatus_ = false;
        return;
    }
    if (sessionOpen_ || mqtt_->connected()) {
        mqtt_->disconnect();
        tls_->stop();
    }
    sessionOpen_ = false;
    gotStatus_ = false;
}

bool BambuFleet::printerReady(int index) const {
    if (!cfg_ || index < 0 || index >= cfg_->printerCount) return false;
    const PrinterProfile &p = cfg_->printers[index];
    return printerIsBambu(p) && p.ip[0] && p.accessCode[0];
}

bool BambuFleet::hasReadyPrinter() const {
    const int n = count();
    for (int i = 0; i < n; ++i) {
        if (printerReady(i)) return true;
    }
    return false;
}

void BambuFleet::applyExternalLive(int index, const PrinterLive &live) {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
        live_[index] = live;
        xSemaphoreGive(lock_);
    }
}

bool BambuFleet::inBackoff(int index) const {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return false;
    if (!skipUntilMs_[index]) return false;
    return static_cast<int32_t>(millis() - skipUntilMs_[index]) < 0;
}

void BambuFleet::noteConnectResult(int index, bool ok) {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    if (ok) {
        failStreak_[index] = 0;
        skipUntilMs_[index] = 0;
        return;
    }
    if (failStreak_[index] < 8) failStreak_[index] += 1;
    uint32_t delayMs = 15000u << (failStreak_[index] > 3 ? 3 : failStreak_[index] - 1);
    if (delayMs > 120000) delayMs = 120000;
    skipUntilMs_[index] = millis() + delayMs;
}

int BambuFleet::nextConfigured(int from) const {
    const int n = count();
    if (n <= 0) return 0;
    const uint32_t now = millis();
    int bestDue = -1;
    uint32_t bestDueAge = 0;
    int bestAny = -1;
    uint32_t bestAnyAge = 0;
    int fallback = -1;
    for (int k = 1; k <= n; ++k) {
        const int i = (from + k) % n;
        if (!printerReady(i)) continue;
        if (inBackoff(i)) {
            if (fallback < 0) fallback = i;
            continue;
        }
        const uint32_t seen = live_[i].lastSeenMs;
        const uint32_t age = seen ? now - seen : 0xFFFFFFFFu;
        if (bestAny < 0 || age > bestAnyAge) {
            bestAny = i;
            bestAnyAge = age;
        }
        if (!dueForPoll(live_[i], now)) continue;
        if (bestDue < 0 || age > bestDueAge) {
            bestDue = i;
            bestDueAge = age;
        }
    }
    if (bestDue >= 0) return bestDue;
    if (bestAny >= 0) return bestAny;
    if (fallback >= 0) return fallback;
    return (from + 1) % n;
}

void BambuFleet::nextPrinter() {
    noteRefreshVisited(pollIndex_);
    disconnectMqtt();
    if (refreshSweep_) {
        const int next = nextRefreshIndex();
        if (next >= 0) pollIndex_ = next;
        else {
            refreshSweep_ = false;
            pollIndex_ = nextConfigured(pollIndex_);
        }
    } else {
        pollIndex_ = nextConfigured(pollIndex_);
    }
}

bool BambuFleet::connectMqtt(const char *ipStr, const char *accessCode, const char *name) {
    IPAddress ip;
    if (!ip.fromString(ipStr)) return false;
    mqtt_->setServer(ip, BAMBU_MQTT_PORT);
    char clientId[40];
    snprintf(clientId, sizeof(clientId), "PandaFarm-%04X-%d", (unsigned)(ESP.getEfuseMac() & 0xFFFF), pollIndex_);
    Serial.printf("[Bambu] MQTT %s @ %s sn=%s\n", name, ipStr,
                  cfg_->printers[pollIndex_].serial[0] ? cfg_->printers[pollIndex_].serial : "(none)");
    sessionOpen_ = true;
    if (mqtt_->connect(clientId, "bblp", accessCode)) {
        strlcpy(sessionIp_, ipStr, sizeof(sessionIp_));
        return true;
    }
    Serial.printf("[Bambu] connect failed rc=%d host=%s\n", mqtt_->state(), ipStr);
    tls_->stop();
    sessionOpen_ = false;
    sessionIp_[0] = '\0';
    return false;
}

bool BambuFleet::connectCurrent() {
    if (!mqtt_ || !tls_ || !wifiReady()) return false;
    const int n = count();
    if (n <= 0) return false;
    if (pollIndex_ < 0 || pollIndex_ >= n) pollIndex_ = 0;
    PrinterProfile &p = cfg_->printers[pollIndex_];
    if (!p.ip[0] || !p.accessCode[0]) {
        if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
            live_[pollIndex_].online = false;
            if (!live_[pollIndex_].lastSeenMs) {
                live_[pollIndex_].state = BambuGcodeState::Offline;
                strlcpy(live_[pollIndex_].lastError,
                        p.accessCode[0] ? "Missing IP" : "Missing access code",
                        sizeof(live_[pollIndex_].lastError));
            }
            xSemaphoreGive(lock_);
        }
        return false;
    }

    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (!live_[pollIndex_].lastSeenMs) live_[pollIndex_].state = BambuGcodeState::Syncing;
        xSemaphoreGive(lock_);
    }

    const char *okHost = nullptr;
    int lastRc = -2;
    if (connectMqtt(p.ip, p.accessCode, p.name)) {
        okHost = p.ip;
    } else {
        lastRc = mqtt_->state();
        if (p.serial[0] && discovery_) {
            for (const auto &d : discovery_->results()) {
                if (!d.ip[0] || strcmp(d.serial, p.serial) != 0 || strcmp(d.ip, p.ip) == 0) continue;
                if (connectMqtt(d.ip, p.accessCode, p.name)) {
                    okHost = d.ip;
                    break;
                }
                lastRc = mqtt_->state();
            }
        }
    }

    if (!okHost) {
        if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
            live_[pollIndex_].online = false;
            if (!live_[pollIndex_].lastSeenMs) {
                live_[pollIndex_].state = BambuGcodeState::Offline;
            }
            strlcpy(live_[pollIndex_].lastError, mqttFailReason(lastRc), sizeof(live_[pollIndex_].lastError));
            xSemaphoreGive(lock_);
        }
        noteConnectResult(pollIndex_, false);
        return false;
    }

    noteConnectResult(pollIndex_, true);

    if (strcmp(p.ip, okHost) != 0) {
        Serial.printf("[Bambu] updating IP %s -> %s\n", p.ip, okHost);
        strlcpy(p.ip, okHost, sizeof(p.ip));
        serialDirty_ = true;
    }

    if (!p.serial[0]) {
        char sn[32] = {};
        if (serialFromTls(sn, sizeof(sn))) {
            strlcpy(p.serial, sn, sizeof(p.serial));
            serialDirty_ = true;
            Serial.printf("[Bambu] serial from TLS cert: %s\n", sn);
        }
    }

    char topic[80];
    if (p.serial[0]) snprintf(topic, sizeof(topic), "device/%s/report", p.serial);
    else strlcpy(topic, "device/#", sizeof(topic));
    const bool subOk = mqtt_->subscribe(topic);
    Serial.printf("[Bambu] connected rc=%d sub=%s ok=%d\n", mqtt_->state(), topic, subOk ? 1 : 0);

    gotStatus_ = false;
    lastStatusMs_ = millis();
    connectStartMs_ = millis();
    lastPushMs_ = 0;
    requestPushAll(p.serial);
    return true;
}

bool BambuFleet::serialFromTls(char *out, size_t outLen) {
    if (!tls_ || !out || outLen == 0) return false;
    out[0] = '\0';
    const mbedtls_x509_crt *crt = tls_->getPeerCertificate();
    if (!crt) return false;
    for (const mbedtls_x509_name *name = &crt->subject; name; name = name->next) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) != 0) continue;
        if (!name->val.p || name->val.len == 0) continue;
        const size_t n = name->val.len < outLen - 1 ? name->val.len : outLen - 1;
        memcpy(out, name->val.p, n);
        out[n] = '\0';
        return out[0] != '\0';
    }
    return false;
}

void BambuFleet::requestPushAll(const char *serial) {
    if (!mqtt_ || !mqtt_->connected() || !serial || !serial[0]) return;
    char req[80];
    snprintf(req, sizeof(req), "device/%s/request", serial);
    const char *body = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
    const bool ok = mqtt_->publish(req, body);
    lastPushMs_ = millis();
    Serial.printf("[Bambu] pushall -> %s ok=%d\n", req, ok ? 1 : 0);
}

void BambuFleet::setFocus(int index) {
    focusIndex_ = index;
}

bool BambuFleet::sendPrintCommand(int index, const char *command) {
    if (index < 0 || index >= count() || !command || !command[0]) return false;
    if (cfg_ && printerIsKlipper(cfg_->printers[index])) {
        return KlipperFleet::instance().sendPrintCommand(index, command);
    }
    pendingIndex_ = index;
    strlcpy(pendingCmd_, command, sizeof(pendingCmd_));
    pendingParam_[0] = '\0';
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (!strcmp(command, "pause")) live_[index].state = BambuGcodeState::Pause;
        else if (!strcmp(command, "resume")) live_[index].state = BambuGcodeState::Running;
        else if (!strcmp(command, "stop")) live_[index].state = BambuGcodeState::Stopped;
        xSemaphoreGive(lock_);
    }
    setFocus(index);
    return true;
}

bool BambuFleet::reprintLast(int index) {
    if (index < 0 || index >= count()) return false;
    if (cfg_ && printerIsKlipper(cfg_->printers[index])) {
        return KlipperFleet::instance().reprintLast(index);
    }
    char file[80] = {};
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
        strlcpy(file, live_[index].gcodeFile, sizeof(file));
        xSemaphoreGive(lock_);
    }
    if (!file[0]) return false;
    pendingIndex_ = index;
    strlcpy(pendingCmd_, "gcode_file", sizeof(pendingCmd_));
    strlcpy(pendingParam_, file, sizeof(pendingParam_));
    setFocus(index);
    return true;
}

void BambuFleet::publishPrintCommand(const char *serial, const char *command, const char *param) {
    if (!mqtt_ || !mqtt_->connected() || !serial || !serial[0] || !command) return;
    char topic[80];
    snprintf(topic, sizeof(topic), "device/%s/request", serial);
    char body[192];
    if (param && param[0]) {
        snprintf(body, sizeof(body),
                 "{\"print\":{\"sequence_id\":\"%u\",\"command\":\"%s\",\"param\":\"%s\"}}",
                 seq_++, command, param);
    } else {
        snprintf(body, sizeof(body),
                 "{\"print\":{\"sequence_id\":\"%u\",\"command\":\"%s\"}}",
                 seq_++, command);
    }
    const bool ok = mqtt_->publish(topic, body);
    Serial.printf("[Bambu] cmd %s param=%s ok=%d\n", command, param && param[0] ? param : "-", ok ? 1 : 0);
}

void BambuFleet::flushPendingCommand() {
    if (!pendingCmd_[0] || !mqtt_ || !mqtt_->connected()) return;
    if (pendingIndex_ != pollIndex_) return;
    const char *serial = cfg_ ? cfg_->printers[pollIndex_].serial : "";
    if (!serial[0]) return;
    publishPrintCommand(serial, pendingCmd_, pendingParam_);
    pendingCmd_[0] = '\0';
    pendingParam_[0] = '\0';
    pendingIndex_ = -1;
    requestPushAll(serial);
}

void BambuFleet::applyPrintJson(int index, const char *gcodeState, int percent, int remainMin, const char *task) {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    PrinterLive &l = live_[index];
    l.online = true;
    l.lastSeenMs = millis();
    l.lastError[0] = '\0';
    if (gcodeState && gcodeState[0]) l.state = bambuParseGcodeState(gcodeState);
    if (percent >= 0) l.percent = percent;
    if (remainMin >= 0) l.remainingMin = remainMin;
    if (task && task[0]) strlcpy(l.taskName, task, sizeof(l.taskName));
    if (l.state == BambuGcodeState::Idle || l.state == BambuGcodeState::Finish ||
        l.state == BambuGcodeState::Failed || l.state == BambuGcodeState::Stopped) {
        if (!task || !task[0]) l.taskName[0] = '\0';
    }
}

void BambuFleet::handlePayload(const char *topic, const uint8_t *payload, unsigned int len) {
    char preview[161];
    clipLog(preview, sizeof(preview), payload ? payload : reinterpret_cast<const uint8_t *>(""), payload ? len : 0);
    Serial.printf("[Bambu] rx len=%u topic=%s data=%s\n", len, topic ? topic : "", preview);

    if (!payload || len < 8) return;

    JsonDocument filter;
    filter["print"]["gcode_state"] = true;
    filter["print"]["mc_percent"] = true;
    filter["print"]["mc_remaining_time"] = true;
    filter["print"]["subtask_name"] = true;
    filter["print"]["gcode_file"] = true;
    filter["print"]["command"] = true;
    filter["print"]["msg"] = true;
    filter["print"]["print_error"] = true;
    filter["print"]["nozzle_temper"] = true;
    filter["print"]["nozzle_target_temper"] = true;
    filter["print"]["bed_temper"] = true;
    filter["print"]["bed_target_temper"] = true;
    filter["print"]["layer_num"] = true;
    filter["print"]["total_layer_num"] = true;
    filter["print"]["spd_mag"] = true;
    filter["print"]["wifi_signal"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, payload, len, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[Bambu] json err=%s\n", err.c_str());
        return;
    }
    JsonObject print = doc["print"];
    if (print.isNull()) {
        Serial.println("[Bambu] no print object (not a status report)");
        return;
    }

    int idx = pollIndex_;
    if (topic && cfg_) {
        const char *dev = strstr(topic, "device/");
        if (dev) {
            dev += 7;
            char sn[32] = {};
            size_t n = 0;
            while (*dev && *dev != '/' && n + 1 < sizeof(sn)) sn[n++] = *dev++;
            sn[n] = '\0';
            if (sn[0]) {
                for (int i = 0; i < cfg_->printerCount; ++i) {
                    if (strcmp(cfg_->printers[i].serial, sn) == 0) {
                        idx = i;
                        break;
                    }
                }
                if (idx >= 0 && idx < cfg_->printerCount && !cfg_->printers[idx].serial[0]) {
                    strlcpy(cfg_->printers[idx].serial, sn, sizeof(cfg_->printers[idx].serial));
                    serialDirty_ = true;
                }
            }
        }
    }

    const char *state = print["gcode_state"] | "";
    const char *cmd = print["command"] | "";
    const int percent = print["mc_percent"] | -1;
    const int remain = print["mc_remaining_time"] | -1;
    const char *task = taskFromJson(print);
    Serial.printf("[Bambu] print cmd=%s gcode_state=%s pct=%d remain=%d task=%s\n",
                  cmd, state[0] ? state : "(empty)", percent, remain, task[0] ? task : "(none)");

    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
        applyPrintJson(idx, state, percent, remain, task);
        PrinterLive &l = live_[idx];
        if (!print["print_error"].isNull()) {
            l.printError = print["print_error"].as<uint32_t>();
            if (l.printError != 0 && l.state != BambuGcodeState::Pause) {
                if (l.state != BambuGcodeState::Failed && l.state != BambuGcodeState::Stopped) {
                    l.state = BambuGcodeState::Failed;
                }
                snprintf(l.lastError, sizeof(l.lastError), "Error %08lX", static_cast<unsigned long>(l.printError));
            }
            if (l.printError == 0 && l.state != BambuGcodeState::Failed) l.lastError[0] = '\0';
        }
        if (!print["nozzle_temper"].isNull()) l.nozzleTemp = print["nozzle_temper"].as<int>();
        if (!print["nozzle_target_temper"].isNull()) l.nozzleTarget = print["nozzle_target_temper"].as<int>();
        if (!print["bed_temper"].isNull()) l.bedTemp = print["bed_temper"].as<int>();
        if (!print["bed_target_temper"].isNull()) l.bedTarget = print["bed_target_temper"].as<int>();
        if (!print["layer_num"].isNull()) l.layer = print["layer_num"].as<int>();
        if (!print["total_layer_num"].isNull()) l.layerTotal = print["total_layer_num"].as<int>();
        if (!print["spd_mag"].isNull()) l.speedPct = print["spd_mag"].as<int>();
        const char *file = print["gcode_file"] | "";
        if (file[0]) strlcpy(l.gcodeFile, file, sizeof(l.gcodeFile));
        const char *wifi = print["wifi_signal"] | "";
        if (wifi[0]) strlcpy(l.wifi, wifi, sizeof(l.wifi));
        xSemaphoreGive(lock_);
    }
    if (state && state[0]) {
        gotStatus_ = true;
        lastStatusMs_ = millis();
    }
}

void BambuFleet::persistPendingSerial() {
    if (!serialDirty_ || !cfg_) return;
    serialDirty_ = false;
    CupboardPreferences::instance().savePrinters(*cfg_);
}

void BambuFleet::loop() {
    // MQTT lives on its own task so LVGL stays smooth.
}

void BambuFleet::taskLoop() {
    WiFiClientSecure tls;
    tls.setInsecure();
    tls.setHandshakeTimeout(8);
    tls.setTimeout(6000);
    PubSubClient mqtt(tls);
    mqtt.setCallback(mqttThunk);
    if (!mqtt.setBufferSize(BAMBU_MQTT_BUFFER)) {
        Serial.println("[Bambu] MQTT buffer alloc failed");
    }
    mqtt.setKeepAlive(20);
    mqtt.setSocketTimeout(6);
    tls_ = &tls;
    mqtt_ = &mqtt;

    for (;;) {
        persistPendingSerial();

        if (paused_) {
            if (sessionOpen_ || (mqtt_ && mqtt_->connected())) disconnectMqtt();
            pauseIdle_ = true;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        pauseIdle_ = false;

        if (focusIndex_ >= 0 && focusIndex_ < count() && pollIndex_ != focusIndex_ &&
            printerReady(focusIndex_)) {
            disconnectMqtt();
            pollIndex_ = focusIndex_;
        }

        if (reloadRequested_) {
            reloadRequested_ = false;
            const bool keepSession = mqtt.connected() && !forceDisconnect_ && pollIndex_ >= 0 &&
                                     pollIndex_ < count() && printerReady(pollIndex_) && sessionIp_[0] &&
                                     strcmp(cfg_->printers[pollIndex_].ip, sessionIp_) == 0;
            forceDisconnect_ = false;
            if (!keepSession) disconnectMqtt();
        }

        if (!cfg_ || count() <= 0 || !wifiReady() || !hasReadyPrinter()) {
            if (sessionOpen_) disconnectMqtt();
            wifiReadyMs_ = 0;
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }

        if (wifiReadyMs_ == 0) wifiReadyMs_ = millis();
        if (millis() - wifiReadyMs_ < 800) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!printerReady(pollIndex_) || (inBackoff(pollIndex_) && !mqtt.connected())) {
            if (sessionOpen_ || mqtt.connected()) disconnectMqtt();
            pollIndex_ = nextConfigured(pollIndex_);
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        if (!mqtt.connected()) {
            sessionOpen_ = false;
            if (!connectCurrent()) {
                nextPrinter();
                vTaskDelay(pdMS_TO_TICKS(40));
                continue;
            }
        }

        mqtt.loop();
        if (!mqtt.connected()) {
            Serial.printf("[Bambu] dropped rc=%d\n", mqtt.state());
            sessionOpen_ = false;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        flushPendingCommand();

        const uint32_t now = millis();
        const bool focused = focusIndex_ >= 0;
        const int n = count();
        const bool stayOnPrinter = !refreshSweep_ && (focused || n <= 1);
        if (cfg_->printers[pollIndex_].serial[0] && now - lastPushMs_ > (stayOnPrinter ? 8000 : 2500) &&
            (stayOnPrinter || !gotStatus_)) {
            requestPushAll(cfg_->printers[pollIndex_].serial);
        }

        const bool timedOut = !gotStatus_ && now - connectStartMs_ > 8000;
        const bool rotate = refreshSweep_ ? gotStatus_
                                         : (!stayOnPrinter && gotStatus_ && now - connectStartMs_ > 1100);
        if (timedOut) {
            Serial.println("[Bambu] no gcode_state after 8s");
            if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
                live_[pollIndex_].online = false;
                if (!live_[pollIndex_].lastSeenMs) {
                    live_[pollIndex_].state = BambuGcodeState::Offline;
                }
                strlcpy(live_[pollIndex_].lastError, "Connected, no status yet", sizeof(live_[pollIndex_].lastError));
                xSemaphoreGive(lock_);
            }
            nextPrinter();
        } else if (rotate) {
            nextPrinter();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
