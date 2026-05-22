#include "processing/weight_processor.hpp"

#include <cstdlib>

#include "config/hardware_config.hpp"
#include "freertos/task.h"

namespace mixer::processing {

LatestWeightStore::LatestWeightStore() : mutex_(xSemaphoreCreateMutex()) {}

LatestWeightStore::~LatestWeightStore() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

void LatestWeightStore::set(const domain::WeightState& state) {
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        state_ = state;
        xSemaphoreGive(mutex_);
    }
}

domain::WeightState LatestWeightStore::get() const {
    domain::WeightState copy{};
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        copy = state_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

WeightProcessor::WeightProcessor(QueueHandle_t input_queue, LatestWeightStore& latest)
    : input_queue_(input_queue), latest_(latest) {
    filters_[0] = &raw_filter_;
    filters_[1] = &ma_1s_filter_;
    filters_[2] = &ma_3s_filter_;
    filters_[3] = &ma_5s_filter_;
    filters_[4] = &ma_10s_filter_;
    filters_[5] = &ma_30s_filter_;
    filters_[6] = &ma_60s_filter_;
    filters_[7] = &exponential_filter_;
}

esp_err_t WeightProcessor::start() {
    if (input_queue_ == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        &WeightProcessor::taskEntry,
        "weight_processor",
        config::kProcessorTaskStackBytes,
        this,
        tskIDLE_PRIORITY + 2,
        nullptr,
        tskNO_AFFINITY);

    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void WeightProcessor::taskEntry(void* context) {
    static_cast<WeightProcessor*>(context)->run();
}

void WeightProcessor::run() {
    while (true) {
        domain::WeightSample sample{};
        if (xQueueReceive(input_queue_, &sample, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        latest_.set(process(sample));
    }
}

domain::WeightSample HardRejectFilter::process(const domain::WeightSample& sample) {
    domain::WeightSample out = sample;
    out.clean_sum = sample.raw_sum;
    out.clean_valid = false;
    out.reject_reason = "";

    if (!sample.valid) {
        out.reject_reason = "sensor_error";
        return out;
    }

    if (!config::kHardRejectEnabled || !has_last_good_) {
        has_last_good_ = true;
        last_good_clean_sum_ = sample.raw_sum;
        out.clean_sum = sample.raw_sum;
        out.clean_valid = true;
        return out;
    }

    const int64_t delta = std::llabs(sample.raw_sum - last_good_clean_sum_);
    if (delta > config::kHardRejectDeltaRawSum) {
        out.clean_sum = last_good_clean_sum_;
        out.clean_valid = false;
        out.reject_reason = "hard_reject";
        return out;
    }

    last_good_clean_sum_ = sample.raw_sum;
    out.clean_sum = sample.raw_sum;
    out.clean_valid = true;
    return out;
}

void HardRejectFilter::reset() {
    has_last_good_ = false;
    last_good_clean_sum_ = 0;
}

domain::WeightState WeightProcessor::process(const domain::WeightSample& sample) {
    domain::WeightState state{};
    domain::WeightSample filtered_sample = hard_reject_filter_.process(sample);
    state.sample = filtered_sample;
    for (std::size_t i = 0; i < filters_.size(); ++i) {
        if (filters_[i] == nullptr) {
            continue;
        }
        state.filters[state.filter_count++] = filters_[i]->apply(filtered_sample);
    }
    return state;
}

}  // пространство имен mixer::processing
