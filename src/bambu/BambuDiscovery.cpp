#include "bambu/BambuDiscovery.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>

namespace {
WiFiUDP gUdp;
constexpr uint16_t kSsdpPort = 1990;
const IPAddress kSsdpGroup(239, 255, 255, 250);

void headerValue(const char *msg, const char *key, char *out, size_t outLen) {
    out[0] = '\0';
    const char *p = strcasestr(msg, key);
    if (!p) return;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') ++p;
    size_t n = 0;
    while (*p && *p != '\r' && *p != '\n' && n + 1 < outLen) {
        out[n++] = *p++;
    }
    out[n] = '\0';
}

void extractSerial(const char *usn, char *out, size_t outLen) {
    out[0] = '\0';
    if (!usn || !usn[0]) return;
    const char *start = usn;
    const char *uuid = strcasestr(usn, "uuid:");
    if (uuid) start = uuid + 5;
    char tmp[80];
    strlcpy(tmp, start, sizeof(tmp));
    char *cut = strstr(tmp, "::");
    if (cut) *cut = '\0';
    strlcpy(out, tmp, outLen);
}
}  // namespace

void BambuDiscovery::begin() {
    if (started_) return;
    if (gUdp.beginMulticast(kSsdpGroup, kSsdpPort)) {
        started_ = true;
        Serial.println("[SSDP] multicast 239.255.255.250:1990");
    } else if (gUdp.begin(kSsdpPort)) {
        started_ = true;
        Serial.println("[SSDP] bound UDP 1990");
    } else {
        Serial.println("[SSDP] bind failed");
    }
}

void BambuDiscovery::startScan(uint32_t windowMs) {
    begin();
    results_.clear();
    scanning_ = true;
    scanUntilMs_ = millis() + windowMs;
    lastSearchMs_ = 0;
    sendSearch();
}

void BambuDiscovery::sendSearch() {
    if (!started_) return;
    lastSearchMs_ = millis();
    const char *payload =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1990\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: urn:bambulab-com:device:3dprinter:1\r\n"
        "MX: 3\r\n"
        "\r\n";
    gUdp.beginPacket(kSsdpGroup, kSsdpPort);
    gUdp.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
    gUdp.endPacket();

    const char *all =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1990\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: ssdp:all\r\n"
        "MX: 3\r\n"
        "\r\n";
    gUdp.beginPacket(kSsdpGroup, 2021);
    gUdp.write(reinterpret_cast<const uint8_t *>(all), strlen(all));
    gUdp.endPacket();
}

void BambuDiscovery::addOrUpdate(const DiscoveredPrinter &found) {
    if (!found.ip[0] && !found.serial[0]) return;
    for (auto &row : results_) {
        if ((found.serial[0] && strcmp(row.serial, found.serial) == 0) ||
            (found.ip[0] && strcmp(row.ip, found.ip) == 0)) {
            if (found.ip[0]) strlcpy(row.ip, found.ip, sizeof(row.ip));
            if (found.serial[0]) strlcpy(row.serial, found.serial, sizeof(row.serial));
            if (found.name[0]) strlcpy(row.name, found.name, sizeof(row.name));
            if (found.model[0]) strlcpy(row.model, found.model, sizeof(row.model));
            return;
        }
    }
    results_.push_back(found);
}

void BambuDiscovery::parsePacket(const char *data, int len, const char *fromIp) {
    if (!data || len <= 0) return;
    if (!strcasestr(data, "bambu") && !strcasestr(data, "3dprinter") &&
        !strcasestr(data, "DevModel.bambu.com") && !strcasestr(data, "urn:bambulab")) {
        return;
    }

    DiscoveredPrinter d{};
    if (fromIp) strlcpy(d.ip, fromIp, sizeof(d.ip));

    char location[64] = {};
    char usn[80] = {};
    headerValue(data, "Location:", location, sizeof(location));
    headerValue(data, "USN:", usn, sizeof(usn));
    headerValue(data, "DevName.bambu.com:", d.name, sizeof(d.name));
    headerValue(data, "DevModel.bambu.com:", d.model, sizeof(d.model));
    if (!d.name[0]) headerValue(data, "DevName.bambulab.com:", d.name, sizeof(d.name));
    if (!d.model[0]) headerValue(data, "DevModel.bambulab.com:", d.model, sizeof(d.model));

    if (location[0]) {
        const char *host = strstr(location, "://");
        const char *ip = host ? host + 3 : location;
        char tmp[32];
        strlcpy(tmp, ip, sizeof(tmp));
        char *slash = strchr(tmp, '/');
        if (slash) *slash = '\0';
        char *colon = strchr(tmp, ':');
        if (colon) *colon = '\0';
        if (tmp[0]) strlcpy(d.ip, tmp, sizeof(d.ip));
    }
    extractSerial(usn, d.serial, sizeof(d.serial));
    if (!d.name[0]) {
        if (d.model[0]) strlcpy(d.name, d.model, sizeof(d.name));
        else if (d.serial[0]) strlcpy(d.name, d.serial, sizeof(d.name));
        else strlcpy(d.name, d.ip, sizeof(d.name));
    }
    addOrUpdate(d);
}

void BambuDiscovery::pollSocket() {
    if (!started_) return;
    int packet = gUdp.parsePacket();
    while (packet > 0) {
        char buf[768];
        const int n = gUdp.read(buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            parsePacket(buf, n, gUdp.remoteIP().toString().c_str());
        }
        packet = gUdp.parsePacket();
    }
}

void BambuDiscovery::loop() {
    if (!started_ && WiFi.status() == WL_CONNECTED) begin();
    pollSocket();
    if (WiFi.status() != WL_CONNECTED) return;
    const uint32_t now = millis();
    if (scanning_) {
        if (now - lastSearchMs_ > 1200 && now < scanUntilMs_) sendSearch();
        if (static_cast<int32_t>(now - scanUntilMs_) >= 0) scanning_ = false;
    } else if (now - lastSearchMs_ > 12000) {
        sendSearch();
    }
}
