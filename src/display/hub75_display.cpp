#include "display/display.hpp"

#include "config/hardware_config.hpp"

#include "display/spinner.hpp"
#include "display/linear_indicator.hpp"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <cstring>

namespace mixer::display
{
    namespace
    {

        constexpr char kTag[] = "hub75_display";

#pragma region Разметка экрана уставок

        // Главная вертикальная шкала: все уставки в overlay-режиме, каждый
        // следующий диапазон снова заполняет колонку снизу вверх.
        constexpr int kAllSetpointsX = 1;
        constexpr int kAllSetpointsY = 12;
        constexpr int kAllSetpointsWidth = 8;
        constexpr int kAllSetpointsHeight = 51;

        // Узкие шкалы отдельных уставок. На 64x64 помещается 6 штук:
        // Все шкалы (и общая, и одиночные) сделаны по 8 пикселей.
        // Одиночные начинаются с X=10 и идут до X=62, оставляя по 1px зазора по бокам и снизу.
        constexpr int kSingleSetpointStartX = 10;
        constexpr int kSingleSetpointY = 12;
        constexpr int kSingleSetpointWidth = 8;
        constexpr int kSingleSetpointHeight = 51;
        constexpr int kSingleSetpointGap = 1;
        constexpr std::size_t kVisibleSingleSetpointCount = 6;

        // Уставки хранятся в raw/MA, поэтому экранные диапазоны тоже строятся
        // в raw. Эти проценты вынесены сюда, чтобы руками менять логику шкал.
        constexpr float kPreviousSetpointBackoff = 0.10f;
        constexpr float kSetpointOverrun = 0.10f;

        // Диагностический spinner вынесен в верхний статус-бар и не перекрывает рабочие шкалы.
        constexpr int kSpinnerX = 57;
        constexpr int kSpinnerY = 2;
        constexpr int kSpinnerWidth = 5;
        constexpr int kSpinnerHeight = 5;

#pragma endregion

#pragma region Вспомогательные утилиты
        /**
         * @brief Генерирует радужный спектр (RGB) на основе фазового сдвига.
         * Используется для циклического изменения цвета анимации.
         * Время использования: Во время показа стартовой анимации.
         *
         * @param phase Текущая фаза/сдвиг (0-255).
         * @param red Ссылка для записи компоненты красного цвета.
         * @param green Ссылка для записи компоненты зеленого цвета.
         * @param blue Ссылка для записи компоненты синего цвета.
         */
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

        const uint8_t* getGlyph3x5(char c)
        {
            static const uint8_t kGlyph0[] = {7, 5, 5, 5, 7}; // 0
            static const uint8_t kGlyph1[] = {2, 2, 2, 2, 2}; // 1
            static const uint8_t kGlyph2[] = {7, 1, 7, 4, 7}; // 2
            static const uint8_t kGlyph3[] = {7, 1, 7, 1, 7}; // 3
            static const uint8_t kGlyph4[] = {5, 5, 7, 1, 1}; // 4
            static const uint8_t kGlyph5[] = {7, 4, 7, 1, 7}; // 5
            static const uint8_t kGlyph6[] = {7, 4, 7, 5, 7}; // 6
            static const uint8_t kGlyph7[] = {7, 1, 1, 1, 1}; // 7
            static const uint8_t kGlyph8[] = {7, 5, 7, 5, 7}; // 8
            static const uint8_t kGlyph9[] = {7, 5, 7, 1, 7}; // 9
            static const uint8_t kGlyphDot[] = {0, 0, 0, 0, 2}; // .
            
            static const uint8_t kGlyphA[] = {2, 5, 7, 5, 5}; // A
            static const uint8_t kGlyphC[] = {7, 4, 4, 4, 7}; // C
            static const uint8_t kGlyphD[] = {6, 5, 5, 5, 6}; // D
            static const uint8_t kGlyphH[] = {5, 5, 7, 5, 5}; // H
            static const uint8_t kGlyphI[] = {7, 2, 2, 2, 7}; // I
            static const uint8_t kGlyphP[] = {7, 5, 7, 4, 4}; // P
            static const uint8_t kGlyphS[] = {7, 4, 7, 1, 7}; // S
            static const uint8_t kGlyphT[] = {7, 2, 2, 2, 2}; // T
            
            static const uint8_t kGlyphSpace[] = {0, 0, 0, 0, 0};
            
            switch (c)
            {
                case '0': return kGlyph0;
                case '1': return kGlyph1;
                case '2': return kGlyph2;
                case '3': return kGlyph3;
                case '4': return kGlyph4;
                case '5': return kGlyph5;
                case '6': return kGlyph6;
                case '7': return kGlyph7;
                case '8': return kGlyph8;
                case '9': return kGlyph9;
                case '.': return kGlyphDot;
                case 'A': return kGlyphA;
                case 'C': return kGlyphC;
                case 'D': return kGlyphD;
                case 'H': return kGlyphH;
                case 'I': return kGlyphI;
                case 'P': return kGlyphP;
                case 'S': return kGlyphS;
                case 'T': return kGlyphT;
                default: return kGlyphSpace;
            }
        }

        void drawChar3x5(MatrixPanel_I2S_DMA* matrix, int x, int y, char c, uint16_t color)
        {
            if (matrix == nullptr) return;
            const uint8_t* rows = getGlyph3x5(c);
            
            uint8_t r = ((color >> 11) & 0x1F) << 3;
            uint8_t g = ((color >> 5) & 0x3F) << 2;
            uint8_t b = (color & 0x1F) << 3;

            for (int row = 0; row < 5; ++row)
            {
                uint8_t val = rows[row];
                if (val & 4) matrix->drawPixelRGB888(x, y + row, r, g, b);
                if (val & 2) matrix->drawPixelRGB888(x + 1, y + row, r, g, b);
                if (val & 1) matrix->drawPixelRGB888(x + 2, y + row, r, g, b);
            }
        }

        void drawText(MatrixPanel_I2S_DMA* matrix, int x, int y, const char* str, uint16_t color)
        {
            while (*str)
            {
                drawChar3x5(matrix, x, y, *str++, color);
                x += 4; // 3px символ + 1px зазор
            }
        }

        float asDisplayValue(int64_t value)
        {
            return static_cast<float>(value);
        }

        float rangeMinimumForAllSetpoints(const DisplayFrame &frame, std::size_t index)
        {
            if (index == 0)
            {
                return 0.0f;
            }

            const float previous = asDisplayValue(frame.setpoints[index - 1].raw_value);
            return previous - std::fabs(previous) * kPreviousSetpointBackoff;
        }

        float rangeMinimumForSingleSetpoint(const DisplayFrame &frame, std::size_t index)
        {
            return index == 0 ? 0.0f : asDisplayValue(frame.setpoints[index - 1].raw_value);
        }

        float rangeMaximumForSetpoint(float setpoint)
        {
            const float maximum = setpoint + std::fabs(setpoint) * kSetpointOverrun;
            return maximum > setpoint ? maximum : setpoint + 1.0f;
        }

        LinearIndicatorBase::Color rangeColor(std::size_t index)
        {
            constexpr std::array<LinearIndicatorBase::Color, 6> colors{{
                {0, 90, 255},
                {0, 210, 70},
                {255, 210, 0},
                {255, 90, 0},
                {210, 0, 255},
                {0, 220, 220},
            }};
            return colors[index % colors.size()];
        }

        LinearIndicatorBase::Color inactiveRangeColor(LinearIndicatorBase::Color color)
        {
            return {
                static_cast<uint8_t>(color.r / 4),
                static_cast<uint8_t>(color.g / 4),
                static_cast<uint8_t>(color.b / 4),
            };
        }
#pragma endregion

    } // анонимное пространство имен

    class Hub75DisplaySink::Impl
    {
    public:
        Impl()
        {
            // Настраиваем спиннер (стиль радара, 1 оборот в секунду, хвост 180 градусов)
            diagnostic_spinner_.setRadarStyle();
            diagnostic_spinner_.setSpeedRpm(config::kHub75SpinnerSpeedRpm);
            diagnostic_spinner_.setTrailLength(180.0f);
            configureStaticIndicatorLayout();
        }

#pragma region Инициализация и запуск
        /**
         * @brief Выполняет первоначальную настройку и запуск матрицы HUB75.
         * Конфигурирует пины GPIO, скорость шины I2S, двойную буферизацию, глубину цвета и яркость.
         * Устанавливает таймер окончания стартовой анимации.
         * Время использования: Однократно при старте приложения (вызывается из Hub75DisplaySink::begin).
         *
         * @return ESP_OK в случае успешного старта, ESP_FAIL — при ошибке инициализации DMA-панели.
         */
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
            // SM16238S-панели требуют стартовой записи регистров, как FM6126A/ICN2038S.
            // Без нее часть экрана может стартовать с другой яркостью после включения.
            matrix_config.driver = HUB75_I2S_CFG::shift_driver::ICN2038S;
            matrix_config.clkphase = true;                                      // clkphase: фаза тактового сигнала (clock phase)
            matrix_config.i2sspeed = HUB75_I2S_CFG::clk_speed::HZ_20M;          // HZ_20M i2sspeed: скорость шины I2S (по умолчанию 20 МГц)
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
            ESP_LOGI(kTag, "HUB75 started: %dx%d chain=%d color_depth=%u brightness=%u driver=ICN2038S",
                     config::kHub75Width,
                     config::kHub75Height,
                     config::kHub75ChainLength,
                     static_cast<unsigned>(config::kHub75ColorDepthBits),
                     static_cast<unsigned>(config::kHub75Brightness));
            startup_animation_until_us_ =
                esp_timer_get_time() + static_cast<int64_t>(config::kHub75StartupAnimationMs) * 1000;
            return ESP_OK;
        }
#pragma endregion

#pragma region Основной цикл отрисовки
        /**
         * @brief Основной метод отрисовки кадра на матрице.
         * Очищает экран, управляет логикой переключения между стартовой анимацией и основным интерфейсом
         * весов (статус-бар, диагностический паттерн, спиннер активности), логирует состояние в консоль.
         * Время использования: Постоянно в цикле отрисовки дисплея (вызывается из Hub75DisplaySink::render).
         *
         * @param frame Текущий кадр данных весов для отображения.
         */
        void render(const DisplayFrame &frame)
        {
            if (matrix_ == nullptr)
                return;

            matrix_->fillScreenRGB888(0, 0, 0);
            if (renderStartupAnimation(frame.diagnostic_tick))
                return;

            // Отрисовываем горизонтальный разделитель темно-серого цвета
            matrix_->fillRect(0, 11, 64, 1, 40, 40, 40);

            // Определяем цвет и рисуем код состояния Wi-Fi (DIS, SCH, STA, AP)
            uint16_t state_color = matrix_->color565(120, 120, 120); // DIS - серый
            if (strcmp(frame.wifi_state_code, "SCH") == 0)
            {
                // Мигаем желтым цветом в режиме поиска
                if ((frame.diagnostic_tick / 10) % 2 == 0) {
                    state_color = matrix_->color565(255, 215, 0);
                } else {
                    state_color = matrix_->color565(60, 50, 0);
                }
            }
            else if (strcmp(frame.wifi_state_code, "STA") == 0)
            {
                state_color = matrix_->color565(0, 220, 80); // STA - зеленый
            }
            else if (strcmp(frame.wifi_state_code, "AP") == 0)
            {
                state_color = matrix_->color565(255, 120, 0); // AP - оранжевый
            }

            drawText(matrix_.get(), 1, 2, frame.wifi_state_code, state_color);

            // Отрисовываем сокращенный IP-адрес с цветовой индикацией
            if (frame.wifi_ip[0] != '\0')
            {
                uint16_t ip_color = matrix_->color565(200, 200, 200); // По умолчанию белый
                if (strcmp(frame.wifi_state_code, "STA") == 0)
                {
                    ip_color = matrix_->color565(0, 190, 255); // STA IP - голубой
                }
                else if (strcmp(frame.wifi_state_code, "AP") == 0)
                {
                    if (frame.wifi_ap_has_clients) {
                        ip_color = matrix_->color565(0, 220, 80); // Есть подключенные клиенты - зеленый
                    } else {
                        ip_color = matrix_->color565(200, 200, 200); // Нет клиентов - белый
                    }
                }
                drawText(matrix_.get(), 22, 2, frame.wifi_ip, ip_color);
            }

            configureIndicators(frame);
            drawVirtualSensorIndicator(frame);
            drawSingleSetpointIndicators(frame);
            drawSpinner();
        }
#pragma endregion

    private:
#pragma region Отрисовка графических элементов (Запуск / Инициализация)
        /**
         * @brief Отрисовывает стартовую переливающуюся анимацию "радуги" по диагонали экрана.
         * Время использования: Первые несколько секунд после включения (время задается в config::kHub75StartupAnimationMs).
         *
         * @param tick Счётчик циклов/тактов для анимации.
         * @return true, если анимация всё ещё проигрывается; false, если время анимации истекло.
         */
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
#pragma endregion

#pragma region Отрисовка графических элементов (Активная работа)

        /**
         * @brief Отрисовывает вращающийся диагностический индикатор активности (спиннер).
         * Перенаправляет вызов инкапсулированному объекту Spinner.
         * Время использования: Постоянно во время работы (при взвешивании).
         */
        void drawSpinner()
        {
            diagnostic_spinner_.draw([this](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
                matrix_->drawPixelRGB888(x, y, r, g, b);
            });
        }

        void configureStaticIndicatorLayout()
        {
            all_setpoints_indicator_.setFrame(true, {28, 28, 28});
            all_setpoints_indicator_.setFillBounds(LinearIndicatorBase::FillBounds::InsideFrame);
            all_setpoints_indicator_.setDirection(LinearIndicatorBase::Direction::Vertical);
            all_setpoints_indicator_.setCompressInactiveRanges(true);

            for (SolidLinearIndicator &indicator : single_setpoint_indicators_)
            {
                indicator.setFrame(true, {24, 24, 24});
                indicator.setFillBounds(LinearIndicatorBase::FillBounds::InsideFrame);
                indicator.setDirection(LinearIndicatorBase::Direction::Vertical);
                indicator.setColor({0, 150, 255});
            }
        }

        void configureIndicators(const DisplayFrame &frame)
        {
            configureAllSetpointsIndicator(frame);
            configureSingleSetpointIndicators(frame);
        }

        void configureAllSetpointsIndicator(const DisplayFrame &frame)
        {
            all_setpoints_indicator_.clearRanges();
            all_setpoints_indicator_.clearSetpoints();

            if (frame.setpoint_count == 0)
            {
                all_setpoints_indicator_.setValueRange(0.0f, 1.0f);
                all_setpoints_indicator_.addRange(0.0f, 1.0f, {20, 20, 20}, {8, 8, 8});
                return;
            }

            const float first_minimum = rangeMinimumForAllSetpoints(frame, 0);
            const float last_setpoint = asDisplayValue(frame.setpoints[frame.setpoint_count - 1].raw_value);
            all_setpoints_indicator_.setValueRange(first_minimum, rangeMaximumForSetpoint(last_setpoint));

            for (std::size_t i = 0; i < frame.setpoint_count; ++i)
            {
                const float setpoint = asDisplayValue(frame.setpoints[i].raw_value);
                const float from = rangeMinimumForAllSetpoints(frame, i);
                const float to = rangeMaximumForSetpoint(setpoint);
                const LinearIndicatorBase::Color color = rangeColor(i);
                all_setpoints_indicator_.addRange(from, to, color, inactiveRangeColor(color));
                all_setpoints_indicator_.addSetpoint(setpoint, {255, 255, 255});
            }
        }

        void configureSingleSetpointIndicators(const DisplayFrame &frame)
        {
            for (std::size_t i = 0; i < single_setpoint_indicators_.size(); ++i)
            {
                SolidLinearIndicator &indicator = single_setpoint_indicators_[i];
                indicator.clearSetpoints();
                indicator.setColor(i < frame.setpoint_count ? rangeColor(i) : LinearIndicatorBase::Color{12, 12, 12});

                if (i >= frame.setpoint_count)
                {
                    indicator.setValueRange(0.0f, 1.0f);
                    continue;
                }

                const float setpoint = asDisplayValue(frame.setpoints[i].raw_value);
                indicator.setValueRange(rangeMinimumForSingleSetpoint(frame, i),
                                        rangeMaximumForSetpoint(setpoint));
                indicator.addSetpoint(setpoint, {255, 255, 255});
            }
        }

        void drawVirtualSensorIndicator(const DisplayFrame &frame)
        {
            all_setpoints_indicator_.draw(
                frame.valid ? asDisplayValue(frame.raw_sum) : 0.0f,
                [this](int x, int y, int width, int height, LinearIndicatorBase::Color color) {
                    matrix_->fillRect(x, y, width, height, color.r, color.g, color.b);
                },
                [this](int x, int y, LinearIndicatorBase::Color color) {
                    matrix_->drawPixelRGB888(x, y, color.r, color.g, color.b);
                });
        }

        void drawSingleSetpointIndicators(const DisplayFrame &frame)
        {
            const float value = frame.valid ? asDisplayValue(frame.raw_sum) : 0.0f;
            for (SolidLinearIndicator &indicator : single_setpoint_indicators_)
            {
                indicator.draw(
                    value,
                    [this](int x, int y, int width, int height, LinearIndicatorBase::Color color) {
                        matrix_->fillRect(x, y, width, height, color.r, color.g, color.b);
                    },
                    [this](int x, int y, LinearIndicatorBase::Color color) {
                        matrix_->drawPixelRGB888(x, y, color.r, color.g, color.b);
                    });
            }
        }
#pragma endregion

        std::unique_ptr<MatrixPanel_I2S_DMA> matrix_{};
        int64_t startup_animation_until_us_ = 0;
        
        Spinner diagnostic_spinner_{kSpinnerX, kSpinnerY, kSpinnerWidth, kSpinnerHeight};
        OverlayLinearIndicator all_setpoints_indicator_{
            kAllSetpointsX,
            kAllSetpointsY,
            kAllSetpointsWidth,
            kAllSetpointsHeight};
        std::array<SolidLinearIndicator, kVisibleSingleSetpointCount> single_setpoint_indicators_{{
            SolidLinearIndicator{kSingleSetpointStartX + 0 * (kSingleSetpointWidth + kSingleSetpointGap),
                                 kSingleSetpointY,
                                 kSingleSetpointWidth,
                                 kSingleSetpointHeight},
            SolidLinearIndicator{kSingleSetpointStartX + 1 * (kSingleSetpointWidth + kSingleSetpointGap),
                                 kSingleSetpointY,
                                 kSingleSetpointWidth,
                                 kSingleSetpointHeight},
            SolidLinearIndicator{kSingleSetpointStartX + 2 * (kSingleSetpointWidth + kSingleSetpointGap),
                                 kSingleSetpointY,
                                 kSingleSetpointWidth,
                                 kSingleSetpointHeight},
            SolidLinearIndicator{kSingleSetpointStartX + 3 * (kSingleSetpointWidth + kSingleSetpointGap),
                                 kSingleSetpointY,
                                 kSingleSetpointWidth,
                                 kSingleSetpointHeight},
            SolidLinearIndicator{kSingleSetpointStartX + 4 * (kSingleSetpointWidth + kSingleSetpointGap),
                                 kSingleSetpointY,
                                 kSingleSetpointWidth,
                                 kSingleSetpointHeight},
            SolidLinearIndicator{kSingleSetpointStartX + 5 * (kSingleSetpointWidth + kSingleSetpointGap),
                                 kSingleSetpointY,
                                 kSingleSetpointWidth,
                                 kSingleSetpointHeight},
        }};
    };

#pragma region Внешний интерфейс Hub75DisplaySink
    /**
     * @brief Конструктор класса Hub75DisplaySink. Создает внутреннюю реализацию (Pimpl).
     * Время использования: Однократно при создании экземпляра класса.
     */
    Hub75DisplaySink::Hub75DisplaySink() : impl_(std::make_unique<Impl>()) {}

    /**
     * @brief Деструктор класса Hub75DisplaySink. Освобождает внутреннюю реализацию.
     * Время использования: Однократно при уничтожении экземпляра класса.
     */
    Hub75DisplaySink::~Hub75DisplaySink() = default;

    /**
     * @brief Выполняет инициализацию дисплея через внутреннюю реализацию.
     * Время использования: Однократно при запуске системы.
     *
     * @return ESP_OK в случае успеха, ESP_FAIL — при ошибке.
     */
    esp_err_t Hub75DisplaySink::begin()
    {
        return impl_->begin();
    }

    /**
     * @brief Выполняет отрисовку кадра через внутреннюю реализацию.
     * Время использования: Постоянно в цикле вывода индикации.
     *
     * @param frame Структура кадра данных для отрисовки.
     */
    void Hub75DisplaySink::render(const DisplayFrame &frame)
    {
        impl_->render(frame);
    }
#pragma endregion

} // пространство имен mixer::display
