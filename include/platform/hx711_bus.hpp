#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config/hardware_config.hpp"

#include "esp_err.h"

namespace mixer::platform {

// Читает несколько HX711 как одну шину, если у них общий SCK. Это нужно потому,
// что библиотечный читатель специально простой и читает микросхемы по очереди,
// а эта шина тактирует все DOUT в одном 24-битном цикле и делает логический
// замер настолько одновременным, насколько позволяет проводка.
class Hx711Bus {
public:
    esp_err_t initialize();
    bool isReady(std::size_t index) const;
    bool waitAllReady(uint32_t timeout_ms) const;
    esp_err_t readRaw(std::array<int32_t, config::kLoadCellCount>& values) const;

private:
    static int gainPulseCount(hx711_gain_t gain);
    esp_err_t validateConfiguration();

    gpio_num_t sck_pin_ = GPIO_NUM_NC;
    hx711_gain_t gain_ = HX711_GAIN_A_128;
    std::array<bool, config::kLoadCellCount> active_{};
    bool initialized_ = false;
};

}  // пространство имен mixer::platform
