#pragma once

#include <array>

#include "domain/weight_types.hpp"
#include "processing/weight_filters.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace mixer::processing {

// Потокобезопасный ящик с последним обработанным состоянием веса. Web и
// индикация читают отсюда и не трогают HX711 или очереди процессора напрямую.
class LatestWeightStore {
public:
    LatestWeightStore();
    ~LatestWeightStore();

    void set(const domain::WeightState& state);
    domain::WeightState get() const;

private:
    mutable SemaphoreHandle_t mutex_ = nullptr;
    domain::WeightState state_{};
};

// Потребитель raw-замеров и владелец алгоритмов фильтрации. Он отделяет сбор
// данных с датчиков от их интерпретации, поэтому тайминги измерения можно
// менять без переписывания Web и индикации.
class WeightProcessor {
public:
    WeightProcessor(QueueHandle_t input_queue, LatestWeightStore& latest);

    esp_err_t start();

private:
    static void taskEntry(void* context);
    void run();
    domain::WeightState process(const domain::WeightSample& sample);

    QueueHandle_t input_queue_ = nullptr;
    LatestWeightStore& latest_;
    RawWeightFilter raw_filter_{};
    MovingAverageWeightFilter moving_average_filter_{config::kMovingAverageWindow};
    ExponentialWeightFilter exponential_filter_{config::kExponentialAlpha};
    std::array<IWeightFilter*, domain::kMaxFilterOutputs> filters_{};
};

}  // пространство имен mixer::processing
