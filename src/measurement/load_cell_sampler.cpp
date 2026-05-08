#include "measurement/load_cell_sampler.hpp"

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

    if (!reader_.waitAllReady(config::kHx711ReadyTimeoutMs)) {
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
    const esp_err_t read_err = reader_.readRaw(sample.raw);
    if (read_err != ESP_OK) {
        ESP_LOGW(kTag, "HX711 read failed: %s", esp_err_to_name(read_err));
        sample.valid = false;
        return sample;
    }

    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (!reader_.isActive(i)) {
            continue;
        }

        sample.channels[i] =
            static_cast<float>(sample.raw[i] - calibration.offsets[i]) * calibration.scales[i];
        sample.total += sample.channels[i];
    }

    sample.weight = sample.total * calibration.global_scale;
    return sample;
}

}  // пространство имен mixer::measurement
