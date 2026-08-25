#pragma once

#include <cstdint>

#ifndef PANDACUPBOARD_VERSION
#define PANDACUPBOARD_VERSION "0.3.1"
#endif

static constexpr int BAMBU_MAX_PRINTERS = 24;
static constexpr int BAMBU_VISIBLE_ROWS = 8;
static constexpr uint16_t BAMBU_MQTT_PORT = 8883;
static constexpr uint16_t KLIPPER_MOONRAKER_PORT = 7125;
static constexpr uint16_t BAMBU_MQTT_BUFFER = 32768;
static constexpr uint16_t DISPLAY_TIMEOUT_MAX_SEC = 12 * 60 * 60;
static constexpr uint16_t DISPLAY_TIMEOUT_MAX_MIN = 12 * 60;
