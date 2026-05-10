#include "measurement/load_cell_reader.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace mixer::measurement {

esp_err_t LoadCellReader::initialize() {
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        active_[i] = config::kLoadCells[i].enabled;
    }

    if constexpr (config::kHx711ReadDriver == config::Hx711ReadDriver::SharedClockBus) {
        return shared_bus_.initialize();
    } else {
        return initializeSequential();
    }
}

bool LoadCellReader::isReady(std::size_t index) const {
    if (index >= config::kLoadCellCount || !active_[index]) {
        return false;
    }

    if constexpr (config::kHx711ReadDriver == config::Hx711ReadDriver::SharedClockBus) {
        return shared_bus_.isReady(index);
    } else {
        return sequential_readers_[index].isReady();
    }
}

bool LoadCellReader::anyReady() const {
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (isReady(i)) {
            return true;
        }
    }

    return false;
}

bool LoadCellReader::waitAllReady(uint32_t timeout_ms) const {
    if constexpr (config::kHx711ReadDriver == config::Hx711ReadDriver::SharedClockBus) {
        return shared_bus_.waitAllReady(timeout_ms);
    } else {
        return waitAllReadySequential(timeout_ms);
    }
}

esp_err_t LoadCellReader::readRaw(
    std::array<int32_t, config::kLoadCellCount>& values) const {
    if constexpr (config::kHx711ReadDriver == config::Hx711ReadDriver::SharedClockBus) {
        return shared_bus_.readRaw(values);
    } else {
        return readRawSequential(values);
    }
}

bool LoadCellReader::isActive(std::size_t index) const {
    return index < config::kLoadCellCount && active_[index];
}

esp_err_t LoadCellReader::initializeSequential() {
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (!active_[i]) {
            continue;
        }

        const esp_err_t err = sequential_readers_[i].initialize(config::kLoadCells[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

bool LoadCellReader::waitAllReadySequential(uint32_t timeout_ms) const {
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() <= deadline) {
        bool all_ready = true;
        for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
            if (active_[i] && !sequential_readers_[i].isReady()) {
                all_ready = false;
                break;
            }
        }

        if (all_ready) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

esp_err_t LoadCellReader::readRawSequential(
    std::array<int32_t, config::kLoadCellCount>& values) const {
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (!active_[i]) {
            continue;
        }

        const esp_err_t err = sequential_readers_[i].readRaw(values[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

}  // пространство имен mixer::measurement
