#include "measurement/load_cell_sampler.hpp"

#include <array>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace mixer::measurement {
namespace {

constexpr char kTag[] = "sampler";

}  // анонимное пространство имен

LoadCellSampler::LoadCellSampler(settings::SettingsStore& settings, QueueHandle_t output_queue)
    : settings_(settings), output_queue_(output_queue) {}

esp_err_t LoadCellSampler::initialize() {
    const esp_err_t err = reader_.initialize();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to initialize load-cell reader: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t LoadCellSampler::start() {
    if (output_queue_ == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        &LoadCellSampler::taskEntry,
        "load_cell_sampler",
        config::kSamplerTaskStackBytes,
        this,
        tskIDLE_PRIORITY + 3,
        nullptr,
        tskNO_AFFINITY);

    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void LoadCellSampler::taskEntry(void* context) {
    static_cast<LoadCellSampler*>(context)->run();
}

void LoadCellSampler::run() {
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        domain::WeightSample sample = readSample();
        if (xQueueSend(output_queue_, &sample, 0) != pdTRUE) {
            domain::WeightSample dropped{};
            xQueueReceive(output_queue_, &dropped, 0);
            xQueueSend(output_queue_, &sample, 0);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(config::kSamplePeriodMs));
    }
}

domain::WeightSample LoadCellSampler::readSample() {
    domain::WeightSample sample{};
    sample.sequence = ++sequence_;
    sample.valid = true;

    const domain::CalibrationState calibration = settings_.calibration();
    sample.sum_offset = calibration.sum_offset;
    sample.sum_scale = calibration.sum_scale;
    const bool all_ready = reader_.waitAllReady(config::kHx711ReadyTimeoutMs);

    if (!all_ready && (!config::kHx711ReadReadySubsetForDiagnostics || !reader_.anyReady())) {
        sample.valid = false;
        for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
            if (reader_.isActive(i) && !reader_.isReady(i)) {
                ESP_LOGW(kTag, "HX711 channel %u is not ready", static_cast<unsigned>(i));
            }
        }
        sample.timestamp_us = esp_timer_get_time();
        return sample;
    }

    sample.timestamp_us = esp_timer_get_time();
    std::array<int32_t, config::kLoadCellCount> raw{};
    const esp_err_t read_err = reader_.readRaw(raw);
    if (read_err != ESP_OK) {
        ESP_LOGW(kTag, "HX711 read failed: %s", esp_err_to_name(read_err));
        sample.valid = false;
        return sample;
    }

    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        sample.raw_sum += raw[i];
    }
    sample.clean_sum = sample.raw_sum;

    bool hardware_error = false;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (reader_.isActive(i)) {
            const int32_t val = raw[i];
            if (val == 8388607 || val == -8388608 || val == -1) {
                hardware_error = true;
                ESP_LOGW(kTag, "HX711 channel %u hardware error: raw=%ld", static_cast<unsigned>(i), static_cast<long>(val));
            }
        }
    }

    if (hardware_error) {
        sample.valid = false;
        sample.clean_valid = false;
        sample.reject_reason = "sensor_error";
        sample.total = 0.0f;
        sample.weight = 0.0f;
        return sample;
    }

    sample.valid = all_ready;
    sample.total = static_cast<float>(static_cast<double>(sample.raw_sum - calibration.sum_offset) *
                                      static_cast<double>(calibration.sum_scale));
    sample.weight = sample.total;
    sample.clean_valid = sample.valid;
    const int64_t now_us = esp_timer_get_time();
    static int64_t last_log_us = 0;
    if (now_us - last_log_us >= static_cast<int64_t>(config::kHx711DiagnosticLogPeriodMs) * 1000) {
        last_log_us = now_us;
        ESP_LOGI(kTag,
                 "HX711 seq=%llu valid=%d raw_sum=%lld weight=%.2f",
                 static_cast<unsigned long long>(sample.sequence),
                 sample.valid ? 1 : 0,
                 static_cast<long long>(sample.raw_sum),
                 sample.weight);
    }
    return sample;
}

}  // пространство имен mixer::measurement
