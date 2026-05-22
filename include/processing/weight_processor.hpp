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

// Hard reject по суммарному виртуальному датчику. Он не сглаживает данные, а
// только не допускает физически невозможный одиночный скачок в clean/MA поток.
class HardRejectFilter {
public:
    domain::WeightSample process(const domain::WeightSample& sample);
    void reset();

private:
    bool has_last_good_ = false;
    int64_t last_good_clean_sum_ = 0;
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
    HardRejectFilter hard_reject_filter_{};
    RawWeightFilter raw_filter_{};
    MovingAverageWeightFilter ma_1s_filter_{1000 / config::kSamplePeriodMs, "ma_1s"};
    MovingAverageWeightFilter ma_3s_filter_{3000 / config::kSamplePeriodMs, "ma_3s"};
    MovingAverageWeightFilter ma_5s_filter_{5000 / config::kSamplePeriodMs, "ma_5s"};
    MovingAverageWeightFilter ma_10s_filter_{10000 / config::kSamplePeriodMs, "ma_10s"};
    MovingAverageWeightFilter ma_30s_filter_{30000 / config::kSamplePeriodMs, "ma_30s"};
    MovingAverageWeightFilter ma_60s_filter_{60000 / config::kSamplePeriodMs, "ma_60s"};
    ExponentialWeightFilter exponential_filter_{config::kExponentialAlpha};
    std::array<IWeightFilter*, domain::kMaxFilterOutputs> filters_{};
};

}  // пространство имен mixer::processing
