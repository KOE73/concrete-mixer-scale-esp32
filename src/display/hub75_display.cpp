#include "display/display.hpp"

#include "config/hardware_config.hpp"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mixer::display
{
    namespace
    {

        constexpr char kTag[] = "hub75_display";
        constexpr int64_t kDiagnosticLogPeriodUs = 1000000;

        uint8_t clampByte(float value)
        {
            return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
        }

        void rainbowColor(uint8_t phase, uint8_t &red, uint8_t &green, uint8_t &blue)
        {
            if (phase < 85)
            {
                red = static_cast<uint8_t>(255 - phase * 3);
                green = static_cast<uint8_t>(phase * 3);
                blue = 0;
                return;
            }

            if (phase < 170)
            {
                phase = static_cast<uint8_t>(phase - 85);
                red = 0;
                green = static_cast<uint8_t>(255 - phase * 3);
                blue = static_cast<uint8_t>(phase * 3);
                return;
            }

            phase = static_cast<uint8_t>(phase - 170);
            red = static_cast<uint8_t>(phase * 3);
            green = 0;
            blue = static_cast<uint8_t>(255 - phase * 3);
        }

    } // анонимное пространство имен

    class Hub75DisplaySink::Impl
    {
    public:
        esp_err_t begin()
        {
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
                config::kHub75Width,       // mx_width: ширина одной панели (64px)
                config::kHub75Height,      // mx_height: высота одной панели (64px)
                config::kHub75ChainLength, // chain_length: количество панелей в цепи (1)
                pins                       // gpio: структура с пинами подключения
            );
            matrix_config.double_buff = false;                                  // double_buff: двойная буферизация (выключена для экономии SRAM)
            matrix_config.driver = HUB75_I2S_CFG::shift_driver::SHIFTREG;       // driver: тип чипа сдвигового регистра (стандартный SHIFT)
            matrix_config.clkphase = true;                                      // clkphase: фаза тактового сигнала (clock phase)
            matrix_config.i2sspeed = HUB75_I2S_CFG::clk_speed::HZ_20M;          //HZ_20M i2sspeed: скорость шины I2S (по умолчанию 20 МГц)
            matrix_config.latch_blanking = 4;                                   // latch_blanking: время гашения латча (latch blanking)
            matrix_config.setPixelColorDepthBits(config::kHub75ColorDepthBits); // Глубина цвета (3 бита)

            matrix_ = std::make_unique<MatrixPanel_I2S_DMA>(matrix_config);
            if (!matrix_->begin())
            {
                matrix_.reset();
                return ESP_FAIL;
            }

            matrix_->setBrightness8(config::kHub75Brightness);
            matrix_->clearScreen();
#if defined(SPIRAM_DMA_BUFFER)
            ESP_LOGW(kTag, "HUB75 DMA framebuffer is configured for PSRAM");
#else
            ESP_LOGI(kTag, "HUB75 DMA framebuffer is configured for internal DMA SRAM");
#endif
#if defined(CONFIG_SPIRAM)
            ESP_LOGI(kTag, "PSRAM is enabled for non-HUB75 allocations");
#endif
            ESP_LOGI(kTag, "HUB75 started: %dx%d chain=%d color_depth=%u brightness=%u",
                     config::kHub75Width,
                     config::kHub75Height,
                     config::kHub75ChainLength,
                     static_cast<unsigned>(config::kHub75ColorDepthBits),
                     static_cast<unsigned>(config::kHub75Brightness));
            startup_animation_until_us_ =
                esp_timer_get_time() + static_cast<int64_t>(config::kHub75StartupAnimationMs) * 1000;
            return ESP_OK;
        }

        void render(const DisplayFrame &frame)
        {
            if (matrix_ == nullptr)
            {
                return;
            }

            matrix_->fillScreenRGB888(0, 0, 0);
            if (renderStartupAnimation(frame.diagnostic_tick))
            {
                drawDiagnosticSpinner(frame.diagnostic_tick);
                logDiagnosticRender(frame);
                return;
            }

            drawDiagnosticPattern();
            if (!frame.valid || frame.target_weight <= 0.0f)
            {
                drawStatusFrame(24, 24, 24, 0.0f);
                drawDiagnosticSpinner(frame.diagnostic_tick);
                logDiagnosticRender(frame);
                return;
            }

            const float progress = std::clamp(frame.weight / frame.target_weight, 0.0f, 1.0f);
            const bool overfilled = frame.remaining_weight < 0.0f;
            if (overfilled)
            {
                drawStatusFrame(180, 24, 24, progress);
            }
            else
            {
                drawStatusFrame(24, 150, 80, progress);
            }

            drawDiagnosticSpinner(frame.diagnostic_tick);
            logDiagnosticRender(frame);
        }

    private:
        bool renderStartupAnimation(uint32_t tick)
        {
            if (esp_timer_get_time() >= startup_animation_until_us_)
            {
                return false;
            }

            const int diagonal_span = config::kHub75Width + config::kHub75Height;
            const int head = static_cast<int>((tick * 6) % diagonal_span);
            for (int band = 0; band < 18; ++band)
            {
                const int diagonal = head - band;
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                rainbowColor(static_cast<uint8_t>((tick * 18 + band * 12) & 0xff),
                             red,
                             green,
                             blue);

                for (int x = 0; x < config::kHub75Width; ++x)
                {
                    const int y = diagonal - x;
                    if (y < 0 || y >= config::kHub75Height)
                    {
                        continue;
                    }

                    matrix_->drawPixelRGB888(x, y, red, green, blue);
                    if (y + 1 < config::kHub75Height)
                    {
                        matrix_->drawPixelRGB888(x, y + 1, red / 3, green / 3, blue / 3);
                    }
                }
            }

            return true;
        }

        void drawDiagnosticPattern()
        {
            constexpr int block = 10;

            matrix_->fillRect(0, 0, block, block, 220, 0, 0);
            matrix_->fillRect(config::kHub75Width - block, 0, block, block, 0, 220, 0);
            matrix_->fillRect(0, config::kHub75Height - block, block, block, 0, 0, 220);
            matrix_->fillRect(config::kHub75Width - block,
                              config::kHub75Height - block,
                              block,
                              block,
                              220,
                              220,
                              220);

            for (int i = 0; i < config::kHub75Width && i < config::kHub75Height; i += 2)
            {
                matrix_->drawPixelRGB888(i, i, 180, 180, 0);
            }
        }

        void drawDiagnosticSpinner(uint32_t tick)
        {
            constexpr int origin_x = 1;
            constexpr int origin_y = 1;
            constexpr int center_x = origin_x + 4;
            constexpr int center_y = origin_y + 4;
            constexpr uint8_t dim = 20;
            constexpr uint8_t bright = 220;
            static constexpr int points[8][2] = {
                {4, 0},
                {7, 1},
                {8, 4},
                {7, 7},
                {4, 8},
                {1, 7},
                {0, 4},
                {1, 1},
            };

            matrix_->fillRect(origin_x, origin_y, 9, 9, 0, 0, 0);
            for (std::size_t i = 0; i < 8; ++i)
            {
                const bool active = i == tick % 8;
                const uint8_t value = active ? bright : dim;
                drawSpinnerLine(center_x,
                                center_y,
                                origin_x + points[i][0],
                                origin_y + points[i][1],
                                value,
                                value,
                                active ? 40 : dim);
            }
        }

        void drawSpinnerLine(int x0, int y0, int x1, int y1, uint8_t red, uint8_t green, uint8_t blue)
        {
            const int dx = x1 - x0;
            const int dy = y1 - y0;
            const int steps = std::max(std::abs(dx), std::abs(dy));
            for (int step = 0; step <= steps; ++step)
            {
                const int x = x0 + dx * step / steps;
                const int y = y0 + dy * step / steps;
                matrix_->drawPixelRGB888(x, y, red, green, blue);
            }
        }

        void logDiagnosticRender(const DisplayFrame &frame)
        {
            const int64_t now = esp_timer_get_time();
            if (now - last_diagnostic_log_us_ < kDiagnosticLogPeriodUs)
            {
                return;
            }

            last_diagnostic_log_us_ = now;
            ESP_LOGI(kTag, "render tick=%u valid=%d weight=%.2f remaining=%.2f",
                     static_cast<unsigned>(frame.diagnostic_tick),
                     frame.valid ? 1 : 0,
                     static_cast<double>(frame.weight),
                     static_cast<double>(frame.remaining_weight));
        }

        void drawStatusFrame(uint8_t red, uint8_t green, uint8_t blue, float progress)
        {
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
            if (filled > 0)
            {
                matrix_->fillRect(bar_x, bar_y, filled, bar_height, red, green, blue);
            }

            const uint8_t glow = clampByte(40.0f + progress * 180.0f);
            matrix_->fillRect(12, 48, 40, 6, glow, glow, 20);
        }

        std::unique_ptr<MatrixPanel_I2S_DMA> matrix_{};
        int64_t last_diagnostic_log_us_ = 0;
        int64_t startup_animation_until_us_ = 0;
    };

    Hub75DisplaySink::Hub75DisplaySink() : impl_(std::make_unique<Impl>()) {}

    Hub75DisplaySink::~Hub75DisplaySink() = default;

    esp_err_t Hub75DisplaySink::begin()
    {
        return impl_->begin();
    }

    void Hub75DisplaySink::render(const DisplayFrame &frame)
    {
        impl_->render(frame);
    }

} // пространство имен mixer::display
