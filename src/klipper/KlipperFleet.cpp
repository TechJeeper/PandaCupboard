#include "klipper/KlipperFleet.h"

#include "bambu/BambuFleet.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cstring>

namespace {
constexpr uint32_t kIdlePollMs = 20000;
constexpr uint32_t kActivePollMs = 2000;
constexpr uint32_t kFocusPollMs = 1200;

bool wifiReady() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}

BambuGcodeState parseKlipperState(const char *text) {
    if (!text || !text[0]) return BambuGcodeState::Unknown;
    if (!strcasecmp(text, "printing")) return BambuGcodeState::Running;
    if (!strcasecmp(text, "paused")) return BambuGcodeState::Pause;
    if (!strcasecmp(text, "complete") || !strcasecmp(text, "completed")) return BambuGcodeState::Finish;
    if (!strcasecmp(text, "cancelled") || !strcasecmp(text, "canceled")) return BambuGcodeState::Stopped;
    if (!strcasecmp(text, "error") || !strcasecmp(text, "shutdown")) return BambuGcodeState::Failed;
    if (!strcasecmp(text, "standby") || !strcasecmp(text, "ready") || !strcasecmp(text, "idle")) {
        return BambuGcodeState::Idle;
    }
    return BambuGcodeState::Unknown;
}

const char *httpFailReason(int code) {
    if (code == 401 || code == 403) return "Bad API key";
    if (code == 404) return "Moonraker not found";
    if (code <= 0) return "Moonraker unreachable";
    return "Moonraker error";
}

bool dueForPoll(const PrinterLive &live, uint32_t now, bool focused) {
    if (!live.lastSeenMs) return true;
    uint32_t interval = kIdlePollMs;
    if (focused) interval = kFocusPollMs;
    else if (bambuStateIsActive(live.state)) interval = kActivePollMs;
    return (now - live.lastSeenMs) >= interval;
}

bool httpExchange(const PrinterProfile &p, const char *path, const char *postBody, char *out, size_t outLen,
                  int *httpCode) {
    if (out && outLen) out[0] = '\0';
    if (httpCode) *httpCode = -1;
    if (!p.ip[0] || !path || !path[0]) return false;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3500);
    http.setTimeout(4500);
    if (!http.begin(client, p.ip, printerListenPort(p), path)) return false;
    if (p.accessCode[0]) http.addHeader("X-Api-Key", p.accessCode);
    if (postBody) http.addHeader("Content-Type", "application/json");

    int code = postBody ? http.POST(postBody) : http.GET();
    if (httpCode) *httpCode = code;

    const bool ok = (code == 200);
    if (ok && out && outLen > 1) {
        const String payload = http.getString();
        strlcpy(out, payload.c_str(), outLen);
    }
    http.end();
    return ok;
}

const char *filenameTail(const char *path) {
    if (!path || !path[0]) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
}  // namespace

KlipperFleet &KlipperFleet::instance() {
    static KlipperFleet inst;
    return inst;
}

void KlipperFleet::taskThunk(void *arg) { static_cast<KlipperFleet *>(arg)->taskLoop(); }

void KlipperFleet::begin(AppConfig *cfg) {
    cfg_ = cfg;
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!started_) {
        started_ = true;
        xTaskCreatePinnedToCore(taskThunk, "klipperHttp", 16384, this, 1, &task_, 1);
    }
}

void KlipperFleet::reload() { reloadRequested_ = true; }

void KlipperFleet::forgetPrinter(int index) {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (pollIndex_ > index) pollIndex_ -= 1;
        for (int i = index; i < BAMBU_MAX_PRINTERS - 1; ++i) {
            skipUntilMs_[i] = skipUntilMs_[i + 1];
            failStreak_[i] = failStreak_[i + 1];
        }
        skipUntilMs_[BAMBU_MAX_PRINTERS - 1] = 0;
        failStreak_[BAMBU_MAX_PRINTERS - 1] = 0;
        if (pendingIndex_ == index) {
            pendingIndex_ = -1;
            pendingCmd_[0] = '\0';
            pendingParam_[0] = '\0';
        } else if (pendingIndex_ > index) {
            pendingIndex_ = pendingIndex_ - 1;
        }
        xSemaphoreGive(lock_);
    }
    reloadRequested_ = true;
}

void KlipperFleet::loop() {}

bool KlipperFleet::printerReady(int index) const {
    if (!cfg_ || index < 0 || index >= cfg_->printerCount) return false;
    const PrinterProfile &p = cfg_->printers[index];
    return printerIsKlipper(p) && p.ip[0];
}

bool KlipperFleet::inBackoff(int index) const {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return false;
    if (!skipUntilMs_[index]) return false;
    return static_cast<int32_t>(millis() - skipUntilMs_[index]) < 0;
}

void KlipperFleet::noteConnectResult(int index, bool ok) {
    if (index < 0 || index >= BAMBU_MAX_PRINTERS) return;
    if (ok) {
        failStreak_[index] = 0;
        skipUntilMs_[index] = 0;
        return;
    }
    if (failStreak_[index] < 8) failStreak_[index] += 1;
    uint32_t delayMs = 8000u << (failStreak_[index] > 3 ? 3 : failStreak_[index] - 1);
    if (delayMs > 60000) delayMs = 60000;
    skipUntilMs_[index] = millis() + delayMs;
}

bool KlipperFleet::hasReadyPrinter() const {
    const int n = cfg_ ? cfg_->printerCount : 0;
    for (int i = 0; i < n; ++i) {
        if (printerReady(i)) return true;
    }
    return false;
}

int KlipperFleet::nextConfigured(int from) const {
    const int n = cfg_ ? cfg_->printerCount : 0;
    if (n <= 0) return 0;
    const uint32_t now = millis();
    const int focus = BambuFleet::instance().focus();
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
        PrinterProfile profile{};
        PrinterLive live{};
        if (!BambuFleet::instance().getPrinter(i, &profile, &live)) continue;
        const uint32_t seen = live.lastSeenMs;
        const uint32_t age = seen ? now - seen : 0xFFFFFFFFu;
        if (bestAny < 0 || age > bestAnyAge) {
            bestAny = i;
            bestAnyAge = age;
        }
        if (!dueForPoll(live, now, focus == i)) continue;
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

bool KlipperFleet::sendPrintCommand(int index, const char *command) {
    if (!printerReady(index) || !command || !command[0]) return false;
    pendingIndex_ = index;
    strlcpy(pendingCmd_, command, sizeof(pendingCmd_));
    pendingParam_[0] = '\0';
    PrinterProfile profile{};
    PrinterLive live{};
    if (BambuFleet::instance().getPrinter(index, &profile, &live)) {
        if (!strcmp(command, "pause")) live.state = BambuGcodeState::Pause;
        else if (!strcmp(command, "resume")) live.state = BambuGcodeState::Running;
        else if (!strcmp(command, "stop")) live.state = BambuGcodeState::Stopped;
        BambuFleet::instance().applyExternalLive(index, live);
    }
    BambuFleet::instance().setFocus(index);
    return true;
}

bool KlipperFleet::reprintLast(int index) {
    if (!printerReady(index)) return false;
    PrinterProfile profile{};
    PrinterLive live{};
    if (!BambuFleet::instance().getPrinter(index, &profile, &live)) return false;
    if (!live.gcodeFile[0]) return false;
    pendingIndex_ = index;
    strlcpy(pendingCmd_, "gcode_file", sizeof(pendingCmd_));
    strlcpy(pendingParam_, live.gcodeFile, sizeof(pendingParam_));
    BambuFleet::instance().setFocus(index);
    return true;
}

bool KlipperFleet::postCommand(int index, const char *path, const char *body) {
    if (!printerReady(index) || !path) return false;
    char resp[256];
    int code = -1;
    const bool ok = httpExchange(cfg_->printers[index], path, body ? body : "{}", resp, sizeof(resp), &code);
    if (!ok) {
        Serial.printf("[Klipper] POST %s code=%d\n", path, code);
    }
    return ok;
}

void KlipperFleet::flushPendingCommand() {
    if (!pendingCmd_[0] || pendingIndex_ < 0) return;
    if (!printerReady(pendingIndex_)) {
        pendingCmd_[0] = '\0';
        pendingParam_[0] = '\0';
        pendingIndex_ = -1;
        return;
    }
    const int idx = pendingIndex_;
    const char *cmd = pendingCmd_;
    bool ok = false;
    if (!strcmp(cmd, "pause")) ok = postCommand(idx, "/printer/print/pause", "{}");
    else if (!strcmp(cmd, "resume")) ok = postCommand(idx, "/printer/print/resume", "{}");
    else if (!strcmp(cmd, "stop")) ok = postCommand(idx, "/printer/print/cancel", "{}");
    else if (!strcmp(cmd, "gcode_file") && pendingParam_[0]) {
        char body[128];
        snprintf(body, sizeof(body), "{\"filename\":\"%s\"}", pendingParam_);
        ok = postCommand(idx, "/printer/print/start", body);
    }
    Serial.printf("[Klipper] cmd %s ok=%d\n", cmd, ok ? 1 : 0);
    pendingCmd_[0] = '\0';
    pendingParam_[0] = '\0';
    pendingIndex_ = -1;
}

bool KlipperFleet::pollPrinter(int index) {
    if (!printerReady(index)) return false;
    PrinterProfile profile{};
    PrinterLive live{};
    if (!BambuFleet::instance().getPrinter(index, &profile, &live)) return false;
    if (!live.lastSeenMs && live.state != BambuGcodeState::Syncing) {
        live.state = BambuGcodeState::Syncing;
        BambuFleet::instance().applyExternalLive(index, live);
    }

    static const char *kQuery =
        "/printer/objects/query?print_stats&display_status&heater_bed&extruder&virtual_sdcard&gcode_move";
    static char body[4096];
    int code = -1;
    if (!httpExchange(profile, kQuery, nullptr, body, sizeof(body), &code)) {
        live.online = false;
        if (!live.lastSeenMs) live.state = BambuGcodeState::Offline;
        strlcpy(live.lastError, httpFailReason(code), sizeof(live.lastError));
        BambuFleet::instance().applyExternalLive(index, live);
        noteConnectResult(index, false);
        return false;
    }

    JsonDocument filter;
    filter["error"]["message"] = true;
    filter["result"]["status"]["print_stats"]["state"] = true;
    filter["result"]["status"]["print_stats"]["filename"] = true;
    filter["result"]["status"]["print_stats"]["print_duration"] = true;
    filter["result"]["status"]["print_stats"]["info"]["current_layer"] = true;
    filter["result"]["status"]["print_stats"]["info"]["total_layer"] = true;
    filter["result"]["status"]["display_status"]["progress"] = true;
    filter["result"]["status"]["virtual_sdcard"]["progress"] = true;
    filter["result"]["status"]["virtual_sdcard"]["file_path"] = true;
    filter["result"]["status"]["extruder"]["temperature"] = true;
    filter["result"]["status"]["extruder"]["target"] = true;
    filter["result"]["status"]["heater_bed"]["temperature"] = true;
    filter["result"]["status"]["heater_bed"]["target"] = true;
    filter["result"]["status"]["gcode_move"]["speed_factor"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        live.online = false;
        strlcpy(live.lastError, "Bad Moonraker JSON", sizeof(live.lastError));
        BambuFleet::instance().applyExternalLive(index, live);
        noteConnectResult(index, false);
        return false;
    }

    const char *errMsg = doc["error"]["message"] | "";
    if (errMsg[0]) {
        live.online = false;
        strlcpy(live.lastError, errMsg, sizeof(live.lastError));
        if (!live.lastSeenMs) live.state = BambuGcodeState::Failed;
        BambuFleet::instance().applyExternalLive(index, live);
        noteConnectResult(index, true);
        return true;
    }

    JsonObject status = doc["result"]["status"];
    if (status.isNull()) {
        live.online = false;
        strlcpy(live.lastError, "No printer status", sizeof(live.lastError));
        BambuFleet::instance().applyExternalLive(index, live);
        noteConnectResult(index, false);
        return false;
    }

    const char *state = status["print_stats"]["state"] | "";
    const char *file = status["print_stats"]["filename"] | "";
    if (!file[0]) file = status["virtual_sdcard"]["file_path"] | "";
    float progress = status["display_status"]["progress"] | -1.0f;
    if (progress < 0) progress = status["virtual_sdcard"]["progress"] | -1.0f;
    const float duration = status["print_stats"]["print_duration"] | -1.0f;

    live.online = true;
    live.lastSeenMs = millis();
    live.lastError[0] = '\0';
    live.printError = 0;
    if (state[0]) live.state = parseKlipperState(state);
    if (progress >= 0.0f) {
        int pct = static_cast<int>(progress * 100.0f + 0.5f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        live.percent = pct;
        if (progress > 0.02f && duration > 1.0f) {
            const float remainSec = duration / progress - duration;
            live.remainingMin = remainSec > 0 ? static_cast<int>(remainSec / 60.0f + 0.5f) : 0;
        }
    }
    if (file[0]) {
        strlcpy(live.gcodeFile, file, sizeof(live.gcodeFile));
        strlcpy(live.taskName, filenameTail(file), sizeof(live.taskName));
    }
    if (live.state == BambuGcodeState::Idle || live.state == BambuGcodeState::Finish ||
        live.state == BambuGcodeState::Failed || live.state == BambuGcodeState::Stopped) {
        if (live.state == BambuGcodeState::Idle) live.taskName[0] = '\0';
        live.remainingMin = -1;
    }
    if (!status["extruder"]["temperature"].isNull()) {
        live.nozzleTemp = static_cast<int>(status["extruder"]["temperature"].as<float>() + 0.5f);
    }
    if (!status["extruder"]["target"].isNull()) {
        live.nozzleTarget = static_cast<int>(status["extruder"]["target"].as<float>() + 0.5f);
    }
    if (!status["heater_bed"]["temperature"].isNull()) {
        live.bedTemp = static_cast<int>(status["heater_bed"]["temperature"].as<float>() + 0.5f);
    }
    if (!status["heater_bed"]["target"].isNull()) {
        live.bedTarget = static_cast<int>(status["heater_bed"]["target"].as<float>() + 0.5f);
    }
    if (!status["print_stats"]["info"]["current_layer"].isNull()) {
        live.layer = status["print_stats"]["info"]["current_layer"].as<int>();
    }
    if (!status["print_stats"]["info"]["total_layer"].isNull()) {
        live.layerTotal = status["print_stats"]["info"]["total_layer"].as<int>();
    }
    if (!status["gcode_move"]["speed_factor"].isNull()) {
        live.speedPct = static_cast<int>(status["gcode_move"]["speed_factor"].as<float>() * 100.0f + 0.5f);
    }

    BambuFleet::instance().applyExternalLive(index, live);
    noteConnectResult(index, true);
    return true;
}

void KlipperFleet::taskLoop() {
    for (;;) {
        if (reloadRequested_) reloadRequested_ = false;
        flushPendingCommand();

        if (!cfg_ || !hasReadyPrinter() || !wifiReady()) {
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }

        const int focus = BambuFleet::instance().focus();
        if (focus >= 0 && printerReady(focus)) pollIndex_ = focus;
        else pollIndex_ = nextConfigured(pollIndex_);

        if (!printerReady(pollIndex_) || inBackoff(pollIndex_)) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        PrinterProfile profile{};
        PrinterLive live{};
        const bool focused = focus == pollIndex_;
        if (BambuFleet::instance().getPrinter(pollIndex_, &profile, &live) &&
            !dueForPoll(live, millis(), focused) && pendingIndex_ != pollIndex_) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        pollPrinter(pollIndex_);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
