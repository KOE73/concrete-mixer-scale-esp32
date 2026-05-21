#include "processing/weight_processor.hpp"

#include <algorithm>

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
    filters_[5] = &exponential_filter_;
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

float AnomalyPreFilter::median5(std::array<float, AnomalyPreFilter::kWindow> values) {
    std::sort(values.begin(), values.end());
    return values[kCenter];
}

domain::WeightSample AnomalyPreFilter::process(const domain::WeightSample& sample) {
    if (!config::kAnomalyFilterEnabled || !sample.valid) {
        return sample;
    }

    domain::WeightSample out = sample;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (!sample.ready[i]) {
            continue;
        }

        // Сдвиг окна: старые значения уходят вправо, новое становится [0].
        for (std::size_t j = kWindow - 1; j > 0; --j) {
            buf_[i][j] = buf_[i][j - 1];
        }
        buf_[i][0] = sample.channels[i];

        if (count_[i] < kWindow) {
            ++count_[i];
        }

        if (count_[i] >= kWindow) {
            out.channels[i] = median5(buf_[i]);
        } else if (count_[i] > kCenter) {
            // До заполнения полного окна уже можно держать постоянную задержку.
            out.channels[i] = buf_[i][kCenter];
        } else {
            // В самом начале истории ещё нет центральной точки окна.
            out.channels[i] = buf_[i][0];
        }
    }

    // Пересчёт суммарного и откалиброванного веса из скорректированных каналов.
    // Коэффициент global_scale восстанавливается из соотношения weight/total
    // исходного сэмпла, чтобы не хранить ссылку на CalibrationState.
    const float global_scale = (sample.total != 0.0f)
        ? (sample.weight / sample.total)
        : 1.0f;

    out.total = 0.0f;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        if (sample.ready[i]) {
            out.total += out.channels[i];
        }
    }
    out.weight = out.total * global_scale;
    return out;
}

void AnomalyPreFilter::reset() {
    buf_ = {};
    count_ = {};
}

domain::WeightState WeightProcessor::process(const domain::WeightSample& sample) {
    domain::WeightState state{};
    domain::WeightSample filtered_sample = anomaly_filter_.process(sample);
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
