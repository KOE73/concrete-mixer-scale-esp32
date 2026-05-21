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

// Предварительный 5-точечный медианный фильтр выбросов по каждому каналу.
// Хранит пять откалиброванных значений на канал и выдаёт медиану окна. Такое
// окно подавляет одиночные и двойные соседние импульсы: нормальные три точки
// остаются большинством. Задержка вывода — 2 сэмпла (около 200 мс).
class AnomalyPreFilter {
public:
    domain::WeightSample process(const domain::WeightSample& sample);
    void reset();

private:
    static constexpr std::size_t kWindow = 5;
    static constexpr std::size_t kCenter = kWindow / 2;

    // buf_[ch][0] — новейшее, buf_[ch][kCenter] — центральное значение окна.
    std::array<std::array<float, kWindow>, config::kLoadCellCount> buf_{};
    std::array<std::size_t, config::kLoadCellCount> count_{};

    static float median5(std::array<float, kWindow> values);
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
    AnomalyPreFilter anomaly_filter_{};
    RawWeightFilter raw_filter_{};
    MovingAverageWeightFilter ma_1s_filter_{10, "ma_1s"};
    MovingAverageWeightFilter ma_3s_filter_{30, "ma_3s"};
    MovingAverageWeightFilter ma_5s_filter_{50, "ma_5s"};
    MovingAverageWeightFilter ma_10s_filter_{100, "ma_10s"};
    ExponentialWeightFilter exponential_filter_{config::kExponentialAlpha};
    std::array<IWeightFilter*, domain::kMaxFilterOutputs> filters_{};
};

}  // пространство имен mixer::processing
