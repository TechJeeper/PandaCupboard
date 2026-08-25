#pragma once

#include <cstdint>

// Pure helpers for farm-list update decisions. Kept out of Screens.cpp so they can be tested
// without LVGL or the ESP32 toolchain.

enum class FleetListPlan : uint8_t { Patch = 0, Reorder = 1, Rebuild = 2 };

inline bool fleetOrderChanged(const int *prev, const int *next, int n) {
    if (n <= 0) return false;
    if (!prev || !next) return true;
    for (int i = 0; i < n; ++i) {
        if (prev[i] != next[i]) return true;
    }
    return false;
}

inline int32_t fleetClampScrollY(int32_t y, int32_t contentH, int32_t viewH) {
    if (y < 0) y = 0;
    const int32_t maxY = contentH > viewH ? contentH - viewH : 0;
    if (y > maxY) y = maxY;
    return y;
}

// builtCount < 0 means the list has never been populated.
inline FleetListPlan fleetListPlan(int builtCount, int newCount, bool orderChanged, bool interacting,
                                   int32_t scrollY, bool force) {
    if (force || builtCount < 0 || newCount != builtCount) return FleetListPlan::Rebuild;
    if (newCount <= 0 || !orderChanged) return FleetListPlan::Patch;
    if (interacting || scrollY != 0) return FleetListPlan::Patch;
    return FleetListPlan::Reorder;
}

// While a reorder is deferred (user is scrolling), keep painting the on-screen row order.
inline const int *fleetPaintOrder(FleetListPlan plan, bool orderChanged, const int *built, const int *sorted) {
    if (plan == FleetListPlan::Patch && orderChanged) return built;
    return sorted;
}

inline bool fleetCommitOrder(FleetListPlan plan, bool orderChanged) {
    return !(plan == FleetListPlan::Patch && orderChanged);
}
