#pragma once

#include <cstdint>

#include "config/hardware_config.hpp"

#include "esp_err.h"
#include "hx711.h"

namespace mixer::platform {

// Тонкий адаптер над esp-idf-lib/hx711 для консервативного пути чтения. Он
// изолирует сторонний API от сэмплера, чтобы можно было переключиться на общую
// SCK-шину без изменения логики измерения.
class Hx711Reader {
public:
    Hx711Reader() = default;

    esp_err_t initialize(const config::Hx711ChannelConfig& config);
    bool isReady() const;
    bool waitReady(uint32_t timeout_ms) const;
    esp_err_t readRaw(int32_t& value) const;

private:
    mutable hx711_t device_{};
    bool initialized_ = false;
};

}  // пространство имен mixer::platform
