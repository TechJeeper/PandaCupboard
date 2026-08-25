#pragma once

#include "cupboard/BuildConfig.h"
#include <Arduino.h>

enum class BambuGcodeState : uint8_t {
    Unknown = 0,
    Offline,
    Syncing,
    Idle,
    Running,
    Pause,
    Prepare,
    Slicing,
    Finish,
    Failed,
    Stopped
};

enum class FleetSort : uint8_t { DeviceStatus = 0, DeviceName = 1 };

enum class PrinterType : uint8_t { BambuLab = 0, Klipper = 1 };

struct PrinterProfile {
    char name[32];
    char ip[16];
    char accessCode[64];
    char serial[32];
    char model[16];
    PrinterType type = PrinterType::BambuLab;
    uint16_t port = 0;
};

inline bool printerIsKlipper(const PrinterProfile &p) { return p.type == PrinterType::Klipper; }
inline bool printerIsBambu(const PrinterProfile &p) { return p.type != PrinterType::Klipper; }

inline uint16_t printerListenPort(const PrinterProfile &p) {
    if (p.port) return p.port;
    return printerIsKlipper(p) ? KLIPPER_MOONRAKER_PORT : BAMBU_MQTT_PORT;
}

inline const char *printerTypeLabel(PrinterType type) {
    return type == PrinterType::Klipper ? "Klipper" : "Bambu Lab";
}

struct PrinterLive {
    BambuGcodeState state = BambuGcodeState::Offline;
    int percent = -1;
    int remainingMin = -1;
    int nozzleTemp = -1;
    int nozzleTarget = -1;
    int bedTemp = -1;
    int bedTarget = -1;
    int layer = -1;
    int layerTotal = -1;
    int speedPct = -1;
    char taskName[80] = {};
    char gcodeFile[80] = {};
    char wifi[16] = {};
    char lastError[48] = {};
    uint32_t printError = 0;
    bool online = false;
    uint32_t lastSeenMs = 0;
};

struct DiscoveredPrinter {
    char ip[16];
    char serial[32];
    char name[32];
    char model[16];
};

inline bool bambuStateIsActive(BambuGcodeState s) {
    return s == BambuGcodeState::Running || s == BambuGcodeState::Pause ||
           s == BambuGcodeState::Prepare || s == BambuGcodeState::Slicing;
}

inline bool bambuStateShowsProgress(BambuGcodeState s) {
    return s == BambuGcodeState::Running || s == BambuGcodeState::Prepare ||
           s == BambuGcodeState::Slicing;
}

inline BambuGcodeState bambuParseGcodeState(const char *text) {
    if (!text || !text[0]) return BambuGcodeState::Unknown;
    if (!strcasecmp(text, "RUNNING")) return BambuGcodeState::Running;
    if (!strcasecmp(text, "PAUSE") || !strcasecmp(text, "PAUSED")) return BambuGcodeState::Pause;
    if (!strcasecmp(text, "PREPARE") || !strcasecmp(text, "PREPARED")) return BambuGcodeState::Prepare;
    if (!strcasecmp(text, "SLICING")) return BambuGcodeState::Slicing;
    if (!strcasecmp(text, "FINISH") || !strcasecmp(text, "FINISHED")) return BambuGcodeState::Finish;
    if (!strcasecmp(text, "FAILED") || !strcasecmp(text, "FAIL") || !strcasecmp(text, "ERROR")) {
        return BambuGcodeState::Failed;
    }
    if (!strcasecmp(text, "STOP") || !strcasecmp(text, "STOPPED") || !strcasecmp(text, "CANCEL") ||
        !strcasecmp(text, "CANCELLED") || !strcasecmp(text, "CANCELED")) {
        return BambuGcodeState::Stopped;
    }
    if (!strcasecmp(text, "IDLE") || !strcasecmp(text, "READY")) return BambuGcodeState::Idle;
    if (!strcasecmp(text, "OFFLINE")) return BambuGcodeState::Offline;
    return BambuGcodeState::Unknown;
}

inline const char *bambuStateLabel(BambuGcodeState s) {
    switch (s) {
        case BambuGcodeState::Running: return "Printing";
        case BambuGcodeState::Pause: return "Paused";
        case BambuGcodeState::Prepare: return "Preparing";
        case BambuGcodeState::Slicing: return "Slicing";
        case BambuGcodeState::Finish: return "Finished";
        case BambuGcodeState::Failed: return "Failed";
        case BambuGcodeState::Stopped: return "Stopped";
        case BambuGcodeState::Idle: return "Idle";
        case BambuGcodeState::Syncing: return "syncing";
        case BambuGcodeState::Offline: return "Offline";
        default: return "Unknown";
    }
}

inline const char *bambuGcodeStateName(BambuGcodeState s) { return bambuStateLabel(s); }

inline void bambuFormatRemaining(int minutes, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (minutes < 0) {
        out[0] = '\0';
        return;
    }
    const int h = minutes / 60;
    const int m = minutes % 60;
    if (h > 0) snprintf(out, outLen, "-%dh%02dmin", h, m);
    else snprintf(out, outLen, "-%dmin", m);
}

inline void bambuFormatRemain(int minutes, char *out, size_t outLen) {
    bambuFormatRemaining(minutes, out, outLen);
}
