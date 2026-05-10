#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config/hardware_config.hpp"
#include "platform/hx711_bus.hpp"
#include "platform/hx711_reader.hpp"

#include "esp_err.h"

namespace mixer::measurement {

// Выбирает физическую стратегию чтения HX711 и скрывает ее от сэмплера.
// Сэмплеру нужен только "один логический замер"; этот класс решает, придет он
// из проверенного esp-idf-lib пути или из общей SCK-шины для почти одновременного чтения.
class LoadCellReader {
public:
    esp_err_t initialize();
    bool isReady(std::size_t index) const;
    bool anyReady() const;
    bool waitAllReady(uint32_t timeout_ms) const;
    esp_err_t readRaw(std::array<int32_t, config::kLoadCellCount>& values) const;
    bool isActive(std::size_t index) const;

private:
    esp_err_t initializeSequential();
    bool waitAllReadySequential(uint32_t timeout_ms) const;
    esp_err_t readRawSequential(std::array<int32_t, config::kLoadCellCount>& values) const;

    std::array<platform::Hx711Reader, config::kLoadCellCount> sequential_readers_{};
    std::array<bool, config::kLoadCellCount> active_{};
    platform::Hx711Bus shared_bus_{};
};

}  // пространство имен mixer::measurement
