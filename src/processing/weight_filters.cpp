#include "processing/weight_filters.hpp"

#include <algorithm>

#include "config/hardware_config.hpp"

namespace mixer::processing {

const char* RawWeightFilter::name() const {
    return "raw";
}

void RawWeightFilter::reset() {}

domain::FilterOutput RawWeightFilter::apply(const domain::WeightSample& sample) {
    return {name(), sample.raw_sum, sample.total, sample.weight, sample.valid};
}

MovingAverageWeightFilter::MovingAverageWeightFilter(std::size_t window, const char* name)
    : window_(std::clamp<std::size_t>(window, 1, config::kMovingAverageMaxWindow)), name_(name) {
    last_output_.name = name_;
}

const char* MovingAverageWeightFilter::name() const {
    return name_;
}

void MovingAverageWeightFilter::reset() {
    clean_sum_values_.fill(0);
    count_ = 0;
    index_ = 0;
    running_sum_ = 0;
    last_output_ = {name_, 0, 0.0f, 0.0f, false};
}

domain::FilterOutput MovingAverageWeightFilter::apply(const domain::WeightSample& sample) {
    if (!sample.valid || !sample.clean_valid) {
        return last_output_;
    }

    if (count_ == window_) {
        running_sum_ -= clean_sum_values_[index_];
    } else {
        ++count_;
    }

    clean_sum_values_[index_] = sample.clean_sum;
    running_sum_ += sample.clean_sum;
    index_ = (index_ + 1) % window_;

    const int64_t average_raw_sum = static_cast<int64_t>(running_sum_ / static_cast<int64_t>(count_));
    const float total = static_cast<float>(
        static_cast<double>(average_raw_sum - sample.sum_offset) *
        static_cast<double>(sample.sum_scale));
    last_output_ = {name(), average_raw_sum, total, total, true};
    return last_output_;
}

ExponentialWeightFilter::ExponentialWeightFilter(float alpha)
    : alpha_(std::clamp(alpha, 0.0f, 1.0f)) {
    last_output_.name = name();
}

const char* ExponentialWeightFilter::name() const {
    return "exponential";
}

void ExponentialWeightFilter::reset() {
    has_value_ = false;
    clean_sum_ = 0.0f;
    last_output_ = {name(), 0, 0.0f, 0.0f, false};
}

domain::FilterOutput ExponentialWeightFilter::apply(const domain::WeightSample& sample) {
    if (!sample.valid || !sample.clean_valid) {
        return last_output_;
    }

    if (!has_value_) {
        clean_sum_ = static_cast<float>(sample.clean_sum);
        has_value_ = true;
    } else {
        clean_sum_ = alpha_ * static_cast<float>(sample.clean_sum) + (1.0f - alpha_) * clean_sum_;
    }

    const int64_t raw_sum = static_cast<int64_t>(clean_sum_);
    const float total = static_cast<float>(
        static_cast<double>(raw_sum - sample.sum_offset) *
        static_cast<double>(sample.sum_scale));
    last_output_ = {name(), raw_sum, total, total, true};
    return last_output_;
}

}  // пространство имен mixer::processing
