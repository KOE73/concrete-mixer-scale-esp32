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
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        sample.ready[i] = reader_.isActive(i) && reader_.isReady(i);
    }

    const bool all_ready = reader_.waitAllReady(config::kHx711ReadyTimeoutMs);
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        sample.ready[i] = reader_.isActive(i) && reader_.isReady(i);
    }

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
    const esp_err_t read_err = reader_.readRaw(sample.raw);
    if (read_err != ESP_OK) {
        ESP_LOGW(kTag, "HX711 read failed: %s", esp_err_to_name(read_err));
        sample.valid = false;
        return sample;
    }

    sample.valid = all_ready;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (!reader_.isActive(i) || !sample.ready[i]) {
            continue;
        }

        sample.channels[i] =
            static_cast<float>(sample.raw[i] - calibration.offsets[i]) * calibration.scales[i];
        sample.total += sample.channels[i];
    }

    sample.weight = sample.total * calibration.global_scale;
    const int64_t now_us = esp_timer_get_time();
    static int64_t last_log_us = 0;
    if (now_us - last_log_us >= static_cast<int64_t>(config::kHx711DiagnosticLogPeriodMs) * 1000) {
        last_log_us = now_us;
        ESP_LOGI(kTag,
                 "HX711 seq=%llu valid=%d ready=[%d,%d,%d] raw=[%ld,%ld,%ld] weight=%.2f",
                 static_cast<unsigned long long>(sample.sequence),
                 sample.valid ? 1 : 0,
                 sample.ready[0] ? 1 : 0,
                 sample.ready[1] ? 1 : 0,
                 sample.ready[2] ? 1 : 0,
                 static_cast<long>(sample.raw[0]),
                 static_cast<long>(sample.raw[1]),
                 static_cast<long>(sample.raw[2]),
                 sample.weight);
    }
    return sample;
}

}  // пространство имен mixer::measurement
