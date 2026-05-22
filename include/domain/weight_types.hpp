#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config/hardware_config.hpp"

namespace mixer::domain {

inline constexpr std::size_t kMaxUnitConversions = 8;
inline constexpr std::size_t kUnitNameMaxLength = 5;
inline constexpr std::size_t kMaxSetpoints = 16;
inline constexpr std::size_t kSetpointNameMaxLength = 24;

// Условная единица для рабочего пересчета raw/MA в "попугаи": лопата, ведро,
// условный кг и другие короткие единицы, которые удобнее реальных килограммов.
struct UnitConversion {
    bool enabled = false;
    char name[kUnitNameMaxLength + 1]{};
    float raw_per_unit = 1.0f;
};

// Уставка в raw/MA единицах. Это не рецепт, а сохраненная контрольная отметка,
// с которой оператор сравнивает текущие MA/попугаи в контроллере и Web.
struct Setpoint {
    bool enabled = false;
    char name[kSetpointNameMaxLength + 1]{};
    int64_t raw_value = 0;
};

// Калибровка виртуального датчика. Отдельные HX711 каналы не калибруются как
// рабочие веса: сначала складываем raw в raw_sum, потом применяем один offset и scale.
struct CalibrationState {
    int64_t sum_offset = config::kDefaultSumOffset;
    float sum_scale = config::kDefaultSumScale;
    std::array<UnitConversion, kMaxUnitConversions> units{};
    uint8_t unit_count = 0;
    std::array<Setpoint, kMaxSetpoints> setpoints{};
    uint8_t setpoint_count = 0;
};

// Один логический замер виртуального датчика. Raw по отдельным HX711 живет только
// во временном буфере чтения и не публикуется в состоянии.
struct WeightSample {
    uint64_t sequence = 0;
    int64_t timestamp_us = 0;
    int64_t raw_sum = 0;
    int64_t clean_sum = 0;
    int64_t sum_offset = config::kDefaultSumOffset;
    float sum_scale = config::kDefaultSumScale;
    float total = 0.0f;
    float weight = 0.0f;
    bool valid = false;
    bool clean_valid = false;
    const char* reject_reason = "";
};

// Один именованный результат обработки того же замера. Несколько фильтров могут
// работать параллельно, а индикация и Web выбирают нужный результат без знания
// реализации алгоритма.
struct FilterOutput {
    const char* name = "";
    int64_t raw_sum = 0;
    float total = 0.0f;
    float weight = 0.0f;
    bool valid = false;
};

inline constexpr std::size_t kMaxFilterOutputs = 10;

// Полное опубликованное состояние: последний физический замер и все результаты
// фильтров. Этим объектом обмениваются процессор, индикация и Web.
struct WeightState {
    WeightSample sample{};
    std::array<FilterOutput, kMaxFilterOutputs> filters{};
    std::size_t filter_count = 0;
};

}  // пространство имен mixer::domain
