#include "display/display.hpp"

#include "config/hardware_config.hpp"

#include "display/spinner.hpp"
#include "display/linear_indicator.hpp"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mixer::display
{
    namespace
    {

        constexpr char kTag[] = "hub75_display";

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
            configureIndicators();
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

            drawChannelIndicators(frame);
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

        void configureIndicators()
        {
            // Пока рецептов и распределения веса по опорам нет, каждому датчику
            // даем одинаковую условную цель: общий целевой вес делится на число
            // каналов. Это не бизнес-логика дозирования, а только стартовая шкала
            // для визуального сравнения трех опор на HUB75.
            constexpr float per_channel_target = config::kDefaultBatchTargetWeight /
                                                 static_cast<float>(config::kLoadCellCount);

            // A1 / rear_left: базовый контрольный вариант. Одна синяя заливка и
            // белая уставка показывают самый простой режим без цветовых зон.
            rear_left_indicator_.setValueRange(0.0f, per_channel_target * 1.5f);
            rear_left_indicator_.setFrame(true, {28, 28, 28});
            rear_left_indicator_.setFillBounds(LinearIndicatorBase::FillBounds::InsideFrame);
            rear_left_indicator_.setColor({45, 120, 230});
            rear_left_indicator_.addSetpoint(per_channel_target, {230, 230, 230});

            // A2 / rear_right: сегментный вариант. Синий, зеленый и оранжевый
            // диапазоны одновременно живут на одной общей шкале. Зеленая зона
            // специально шире, чтобы "допуск" занимал больше пикселей и был
            // заметнее на матрице 64x64.
            rear_right_indicator_.setValueRange(0.0f, per_channel_target * 1.5f);
            rear_right_indicator_.setFrame(true, {40, 40, 40});
            rear_right_indicator_.setFillBounds(LinearIndicatorBase::FillBounds::IncludeFrame);
            rear_right_indicator_.addRange(0.0f, per_channel_target * 0.45f, {35, 90, 220});
            rear_right_indicator_.addRange(per_channel_target * 0.45f,
                                           per_channel_target * 1.15f,
                                           {40, 180, 80});
            rear_right_indicator_.addRange(per_channel_target * 1.15f,
                                           per_channel_target * 1.5f,
                                           {230, 120, 20});
            rear_right_indicator_.addSetpoint(per_channel_target, {255, 255, 255});

            // A3 / front_support: слойный вариант. Каждый следующий диапазон
            // снова заполняет всю ширину шкалы от нуля и перекрывает предыдущий
            // цвет. Так активный диапазон получает максимум пикселей, а уставки
            // появляются только после входа значения в соответствующую область.
            // color1 - активный цвет текущего слоя, color2 - приглушенный цвет
            // уже пройденного слоя. Сжатие по высоте оставляет историю диапазона
            // видимой, но освобождает визуальный вес для текущего слоя.
            front_support_indicator_.setValueRange(0.0f, per_channel_target * 1.5f);
            front_support_indicator_.setFrame(true, {28, 28, 28});
            front_support_indicator_.setFillBounds(LinearIndicatorBase::FillBounds::InsideFrame);
            front_support_indicator_.setCompressInactiveRanges(true);
            front_support_indicator_.addRange(0.0f,
                                              per_channel_target * 0.35f,
                                              {0, 0, 255},
                                              {0,0, 70});
            front_support_indicator_.addRange(per_channel_target * 0.35f,
                                              per_channel_target * 1.2f,
                                              {0, 255, 0},
                                              {0, 70, 0});
            front_support_indicator_.addRange(per_channel_target * 1.2f,
                                              per_channel_target * 1.5f,
                                              {255, 255, 0},
                                              {255, 255, 0});
            front_support_indicator_.addSetpoint(per_channel_target * 0.8f, {255, 255, 0});
            front_support_indicator_.addSetpoint(per_channel_target, {255, 255, 255});
            front_support_indicator_.addSetpoint(per_channel_target * 1.2f, {255, 255, 0});

            // Отдельная вертикальная шкала нужна как проверка того, что общий
            // виджет умеет работать не только слева направо, но и снизу вверх.
            // Она показывает общий вес тем же overlay-принципом, а место под нее
            // освобождено вместо старых боковых квадратиков готовности каналов.
            total_vertical_indicator_.setValueRange(0.0f, config::kDefaultBatchTargetWeight * 1.5f);
            total_vertical_indicator_.setFrame(true, {28, 28, 28});
            total_vertical_indicator_.setFillBounds(LinearIndicatorBase::FillBounds::InsideFrame);
            total_vertical_indicator_.setDirection(LinearIndicatorBase::Direction::Vertical);
            total_vertical_indicator_.setCompressInactiveRanges(true);
            total_vertical_indicator_.addRange(0.0f,
                                               config::kDefaultBatchTargetWeight * 0.35f,
                                               {0, 0, 255},
                                               {0, 0, 70});
            total_vertical_indicator_.addRange(config::kDefaultBatchTargetWeight * 0.35f,
                                               config::kDefaultBatchTargetWeight * 1.2f,
                                               {0, 255, 0},
                                               {0, 70, 0});
            total_vertical_indicator_.addRange(config::kDefaultBatchTargetWeight * 1.2f,
                                               config::kDefaultBatchTargetWeight * 1.5f,
                                               {255, 255, 0},
                                               {255, 255, 0});
            total_vertical_indicator_.addSetpoint(config::kDefaultBatchTargetWeight, {255, 255, 255});
        }

        void drawChannelIndicators(const DisplayFrame &frame)
        {
            const std::array<LinearIndicatorBase*, config::kLoadCellCount> indicators{
                &rear_left_indicator_,
                &rear_right_indicator_,
                &front_support_indicator_,
            };

            total_vertical_indicator_.draw(
                frame.valid ? frame.weight : 0.0f,
                [this](int x, int y, int width, int height, LinearIndicatorBase::Color color) {
                    matrix_->fillRect(x, y, width, height, color.r, color.g, color.b);
                },
                [this](int x, int y, LinearIndicatorBase::Color color) {
                    matrix_->drawPixelRGB888(x, y, color.r, color.g, color.b);
                });

            for (std::size_t i = 0; i < config::kLoadCellCount; ++i)
            {
                indicators[i]->draw(
                    frame.valid ? frame.channel_weights[i] : 0.0f,
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
        
        Spinner diagnostic_spinner_{1, 1, 9, 9};
        SolidLinearIndicator rear_left_indicator_{14, 12, 48, 10};
        SegmentedLinearIndicator rear_right_indicator_{14, 28, 48, 10};
        OverlayLinearIndicator front_support_indicator_{14, 44, 48, 10};
        OverlayLinearIndicator total_vertical_indicator_{4, 12, 6, 42};
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
