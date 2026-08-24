#pragma once

#include "bambu/BambuTypes.h"
#include <vector>

class BambuDiscovery {
public:
    void begin();
    void loop();
    void startScan(uint32_t windowMs = 4000);
    bool isScanning() const { return scanning_; }
    const std::vector<DiscoveredPrinter> &results() const { return results_; }

private:
    void sendSearch();
    void pollSocket();
    void parsePacket(const char *data, int len, const char *fromIp);
    void addOrUpdate(const DiscoveredPrinter &found);

    std::vector<DiscoveredPrinter> results_;
    bool started_ = false;
    bool scanning_ = false;
    uint32_t scanUntilMs_ = 0;
    uint32_t lastSearchMs_ = 0;
};
