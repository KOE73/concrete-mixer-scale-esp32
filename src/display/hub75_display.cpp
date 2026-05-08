#include "display/display.hpp"

#include "config/hardware_config.hpp"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mixer::display {
namespace {

uint8_t clampByte(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
}

}  // анонимное пространство имен

class Hub75DisplaySink::Impl {
public:
    esp_err_t begin() {
        HUB75_I2S_CFG::i2s_pins pins = {
            config::kHub75R1Pin,
            config::kHub75G1Pin,
            config::kHub75B1Pin,
            config::kHub75R2Pin,
            config::kHub75G2Pin,
            config::kHub75B2Pin,
            config::kHub75APin,
            config::kHub75BPin,
            config::kHub75CPin,
            config::kHub75DPin,
            config::kHub75EPin,
            config::kHub75LatPin,
            config::kHub75OePin,
            config::kHub75ClkPin,
        };

        HUB75_I2S_CFG matrix_config(
            config::kHub75Width,
            config::kHub75Height,
            config::kHub75ChainLength,
            pins);
        matrix_config.setPixelColorDepthBits(5);

        matrix_ = std::make_unique<MatrixPanel_I2S_DMA>(matrix_config);
        if (!matrix_->begin()) {
            matrix_.reset();
            return ESP_FAIL;
        }

        matrix_->setBrightness8(config::kHub75Brightness);
        matrix_->clearScreen();
        return ESP_OK;
    }

    void render(const DisplayFrame& frame) {
        if (matrix_ == nullptr) {
            return;
        }

        matrix_->fillScreenRGB888(0, 0, 0);
        if (!frame.valid || frame.target_weight <= 0.0f) {
            drawStatusFrame(24, 24, 24, 0.0f);
            return;
        }

        const float progress = std::clamp(frame.weight / frame.target_weight, 0.0f, 1.0f);
        const bool overfilled = frame.remaining_weight < 0.0f;
        if (overfilled) {
            drawStatusFrame(180, 24, 24, progress);
        } else {
            drawStatusFrame(24, 150, 80, progress);
        }
    }

private:
    void drawStatusFrame(uint8_t red, uint8_t green, uint8_t blue, float progress) {
        constexpr int margin = 4;
        constexpr int bar_x = 8;
        constexpr int bar_y = 24;
        constexpr int bar_width = config::kHub75Width - 16;
        constexpr int bar_height = 16;

        matrix_->fillRect(margin, margin, config::kHub75Width - margin * 2, 2, 40, 40, 40);
        matrix_->fillRect(margin, config::kHub75Height - margin - 2,
                          config::kHub75Width - margin * 2, 2, 40, 40, 40);
        matrix_->fillRect(margin, margin, 2, config::kHub75Height - margin * 2, 40, 40, 40);
        matrix_->fillRect(config::kHub75Width - margin - 2, margin, 2,
                          config::kHub75Height - margin * 2, 40, 40, 40);

        matrix_->fillRect(bar_x, bar_y, bar_width, bar_height, 12, 12, 12);
        const int filled = static_cast<int>(std::round(progress * static_cast<float>(bar_width)));
        if (filled > 0) {
            matrix_->fillRect(bar_x, bar_y, filled, bar_height, red, green, blue);
        }

        const uint8_t glow = clampByte(40.0f + progress * 180.0f);
        matrix_->fillRect(12, 48, 40, 6, glow, glow, 20);
    }

    std::unique_ptr<MatrixPanel_I2S_DMA> matrix_{};
};

Hub75DisplaySink::Hub75DisplaySink() : impl_(std::make_unique<Impl>()) {}

Hub75DisplaySink::~Hub75DisplaySink() = default;

esp_err_t Hub75DisplaySink::begin() {
    return impl_->begin();
}

void Hub75DisplaySink::render(const DisplayFrame& frame) {
    impl_->render(frame);
}

}  // пространство имен mixer::display
