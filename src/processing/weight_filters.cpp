#include "processing/weight_filters.hpp"

#include <algorithm>

#include "config/hardware_config.hpp"

namespace mixer::processing {

const char* RawWeightFilter::name() const {
    return "raw";
}

void RawWeightFilter::reset() {}

domain::FilterOutput RawWeightFilter::apply(const domain::WeightSample& sample) {
    return {name(), sample.total, sample.weight, sample.valid};
}

MovingAverageWeightFilter::MovingAverageWeightFilter(std::size_t window)
    : window_(std::clamp<std::size_t>(window, 1, config::kMovingAverageWindow)) {}

const char* MovingAverageWeightFilter::name() const {
    return "moving_average";
}

void MovingAverageWeightFilter::reset() {
    total_values_.fill(0.0f);
    weight_values_.fill(0.0f);
    count_ = 0;
    index_ = 0;
}

domain::FilterOutput MovingAverageWeightFilter::apply(const domain::WeightSample& sample) {
    if (!sample.valid) {
        return {name(), 0.0f, 0.0f, false};
    }

    total_values_[index_] = sample.total;
    weight_values_[index_] = sample.weight;
    index_ = (index_ + 1) % window_;
    if (count_ < window_) {
        ++count_;
    }

    float total_sum = 0.0f;
    float weight_sum = 0.0f;
    for (std::size_t i = 0; i < count_; ++i) {
        total_sum += total_values_[i];
        weight_sum += weight_values_[i];
    }

    const float count = static_cast<float>(count_);
    return {name(), total_sum / count, weight_sum / count, true};
}

ExponentialWeightFilter::ExponentialWeightFilter(float alpha)
    : alpha_(std::clamp(alpha, 0.0f, 1.0f)) {}

const char* ExponentialWeightFilter::name() const {
    return "exponential";
}

void ExponentialWeightFilter::reset() {
    has_value_ = false;
    total_ = 0.0f;
    weight_ = 0.0f;
}

domain::FilterOutput ExponentialWeightFilter::apply(const domain::WeightSample& sample) {
    if (!sample.valid) {
        return {name(), 0.0f, 0.0f, false};
    }

    if (!has_value_) {
        total_ = sample.total;
        weight_ = sample.weight;
        has_value_ = true;
    } else {
        total_ = alpha_ * sample.total + (1.0f - alpha_) * total_;
        weight_ = alpha_ * sample.weight + (1.0f - alpha_) * weight_;
    }

    return {name(), total_, weight_, true};
}

}  // пространство имен mixer::processing
