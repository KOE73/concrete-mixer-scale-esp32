#include "platform/hx711_bus.hpp"

#include <cstddef>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace mixer::platform {

esp_err_t Hx711Bus::initialize() {
    esp_err_t err = validateConfiguration();
    if (err != ESP_OK) {
        return err;
    }

    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        active_[i] = config::kLoadCells[i].enabled;
        if (!active_[i]) {
            continue;
        }

        gpio_config_t dout_config{};
        dout_config.pin_bit_mask = 1ULL << static_cast<uint32_t>(config::kLoadCells[i].dout_pin);
        dout_config.mode = GPIO_MODE_INPUT;
        dout_config.pull_up_en = GPIO_PULLUP_ENABLE;
        dout_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        dout_config.intr_type = GPIO_INTR_DISABLE;
        err = gpio_config(&dout_config);
        if (err != ESP_OK) {
            return err;
        }
    }

    gpio_config_t sck_config{};
    sck_config.pin_bit_mask = 1ULL << static_cast<uint32_t>(sck_pin_);
    sck_config.mode = GPIO_MODE_OUTPUT;
    sck_config.pull_up_en = GPIO_PULLUP_DISABLE;
    sck_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    sck_config.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&sck_config);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(sck_pin_, 0);
    initialized_ = true;
    return ESP_OK;
}

bool Hx711Bus::isReady(std::size_t index) const {
    if (!initialized_ || index >= config::kLoadCellCount || !active_[index]) {
        return false;
    }

    return gpio_get_level(config::kLoadCells[index].dout_pin) == 0;
}

bool Hx711Bus::waitAllReady(uint32_t timeout_ms) const {
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() <= deadline) {
        bool all_ready = true;
        for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
            if (active_[i] && !isReady(i)) {
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

esp_err_t Hx711Bus::readRaw(std::array<int32_t, config::kLoadCellCount>& values) const {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    std::array<uint32_t, config::kLoadCellCount> unsigned_values{};
    for (int bit = 0; bit < 24; ++bit) {
        gpio_set_level(sck_pin_, 1);
        esp_rom_delay_us(1);

        for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
            if (!active_[i]) {
                continue;
            }
            unsigned_values[i] =
                (unsigned_values[i] << 1) |
                static_cast<uint32_t>(gpio_get_level(config::kLoadCells[i].dout_pin));
        }

        gpio_set_level(sck_pin_, 0);
        esp_rom_delay_us(1);
    }

    const int pulses = gainPulseCount(gain_);
    if (pulses <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int pulse = 0; pulse < pulses; ++pulse) {
        gpio_set_level(sck_pin_, 1);
        esp_rom_delay_us(1);
        gpio_set_level(sck_pin_, 0);
        esp_rom_delay_us(1);
    }

    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (!active_[i]) {
            continue;
        }

        if ((unsigned_values[i] & 0x800000U) != 0) {
            unsigned_values[i] |= 0xFF000000U;
        }
        values[i] = static_cast<int32_t>(unsigned_values[i]);
    }

    return ESP_OK;
}

int Hx711Bus::gainPulseCount(hx711_gain_t gain) {
    switch (gain) {
        case HX711_GAIN_A_128:
            return 1;
        case HX711_GAIN_B_32:
            return 2;
        case HX711_GAIN_A_64:
            return 3;
        default:
            return 0;
    }
}

esp_err_t Hx711Bus::validateConfiguration() {
    bool found_active = false;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        const config::Hx711ChannelConfig& channel = config::kLoadCells[i];
        if (!channel.enabled) {
            continue;
        }

        if (!found_active) {
            sck_pin_ = channel.sck_pin;
            gain_ = channel.gain;
            found_active = true;
            continue;
        }

        if (channel.sck_pin != sck_pin_ || channel.gain != gain_) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    return found_active ? ESP_OK : ESP_ERR_INVALID_STATE;
}

}  // пространство имен mixer::platform
