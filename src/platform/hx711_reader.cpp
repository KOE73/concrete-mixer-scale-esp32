#include "platform/hx711_reader.hpp"

namespace mixer::platform {

esp_err_t Hx711Reader::initialize(const config::Hx711ChannelConfig& config) {
    device_ = {};
    device_.dout = config.dout_pin;
    device_.pd_sck = config.sck_pin;
    device_.gain = config.gain;

    const esp_err_t err = hx711_init(&device_);
    initialized_ = err == ESP_OK;
    return err;
}

bool Hx711Reader::isReady() const {
    if (!initialized_) {
        return false;
    }

    bool ready = false;
    return hx711_is_ready(&device_, &ready) == ESP_OK && ready;
}

bool Hx711Reader::waitReady(uint32_t timeout_ms) const {
    return initialized_ && hx711_wait(&device_, timeout_ms) == ESP_OK;
}

esp_err_t Hx711Reader::readRaw(int32_t& value) const {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    return hx711_read_data(&device_, &value);
}

}  // пространство имен mixer::platform
