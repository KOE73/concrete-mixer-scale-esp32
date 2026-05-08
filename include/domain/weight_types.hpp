#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config/hardware_config.hpp"

namespace mixer::domain {

// Калибровка, которая применяется сразу после чтения raw-значений АЦП. Это
// простой тип данных, чтобы NVS, Web и сэмплер обменивались одним контрактом
// без зависимости от классов друг друга.
struct CalibrationState {
    std::array<int32_t, config::kLoadCellCount> offsets{};
    std::array<float, config::kLoadCellCount> scales{};
    float global_scale = config::kDefaultGlobalScale;
};

// Один логический замер со всех включенных тензодатчиков. Raw-значения хранятся
// рядом с откалиброванными весами каналов, чтобы Web и отладка показывали и
// электрический сигнал, и значение, которое ушло в фильтрацию.
struct WeightSample {
    uint64_t sequence = 0;
    int64_t timestamp_us = 0;
    std::array<int32_t, config::kLoadCellCount> raw{};
    std::array<float, config::kLoadCellCount> channels{};
    float total = 0.0f;
    float weight = 0.0f;
    bool valid = false;
};

// Один именованный результат обработки того же замера. Несколько фильтров могут
// работать параллельно, а индикация и Web выбирают нужный результат без знания
// реализации алгоритма.
struct FilterOutput {
    const char* name = "";
    float total = 0.0f;
    float weight = 0.0f;
    bool valid = false;
};

inline constexpr std::size_t kMaxFilterOutputs = 3;

// Полное опубликованное состояние: последний физический замер и все результаты
// фильтров. Этим объектом обмениваются процессор, индикация и Web.
struct WeightState {
    WeightSample sample{};
    std::array<FilterOutput, kMaxFilterOutputs> filters{};
    std::size_t filter_count = 0;
};

}  // пространство имен mixer::domain
