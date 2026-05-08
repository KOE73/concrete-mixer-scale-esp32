#pragma once

#include <array>
#include <cstddef>

#include "config/hardware_config.hpp"
#include "domain/weight_types.hpp"

namespace mixer::processing {

// Общий интерфейс для заменяемых алгоритмов фильтрации. WeightProcessor держит
// небольшой набор таких фильтров, чтобы одновременно были доступны несколько
// интерпретаций одного физического замера.
class IWeightFilter {
public:
    virtual ~IWeightFilter() = default;

    virtual const char* name() const = 0;
    virtual void reset() = 0;
    virtual domain::FilterOutput apply(const domain::WeightSample& sample) = 0;
};

// Фильтр без сглаживания, базовая точка сравнения. Он полезен для отладки,
// потому что показывает ровно то, что дала калибровка до сглаживания.
class RawWeightFilter final : public IWeightFilter {
public:
    const char* name() const override;
    void reset() override;
    domain::FilterOutput apply(const domain::WeightSample& sample) override;
};

// Сглаживание скользящим окном для шумных показаний бетономешалки. Оно меняет
// скорость реакции на стабильные значения для индикации и Web.
class MovingAverageWeightFilter final : public IWeightFilter {
public:
    explicit MovingAverageWeightFilter(std::size_t window);

    const char* name() const override;
    void reset() override;
    domain::FilterOutput apply(const domain::WeightSample& sample) override;

private:
    std::array<float, config::kMovingAverageWindow> total_values_{};
    std::array<float, config::kMovingAverageWindow> weight_values_{};
    std::size_t window_ = config::kMovingAverageWindow;
    std::size_t count_ = 0;
    std::size_t index_ = 0;
};

// Легкий IIR-фильтр. Это второй вариант сглаживания: памяти нужно меньше, чем
// окну, а поведение при заполнении емкости получается другим.
class ExponentialWeightFilter final : public IWeightFilter {
public:
    explicit ExponentialWeightFilter(float alpha);

    const char* name() const override;
    void reset() override;
    domain::FilterOutput apply(const domain::WeightSample& sample) override;

private:
    float alpha_ = config::kExponentialAlpha;
    bool has_value_ = false;
    float total_ = 0.0f;
    float weight_ = 0.0f;
};

}  // пространство имен mixer::processing
