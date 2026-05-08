#include "processing/weight_processor.hpp"

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
    filters_[1] = &moving_average_filter_;
    filters_[2] = &exponential_filter_;
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

domain::WeightState WeightProcessor::process(const domain::WeightSample& sample) {
    domain::WeightState state{};
    state.sample = sample;
    for (std::size_t i = 0; i < filters_.size(); ++i) {
        if (filters_[i] == nullptr) {
            continue;
        }
        state.filters[state.filter_count++] = filters_[i]->apply(sample);
    }
    return state;
}

}  // пространство имен mixer::processing
