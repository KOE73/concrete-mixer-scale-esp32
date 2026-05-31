#pragma once

#include "config/hardware_config.hpp"
#include "domain/weight_types.hpp"
#include "processing/weight_processor.hpp"
#include "settings/settings_store.hpp"

#include "esp_err.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mixer::display
{

    // DTO для передачи данных в дисплей. Содержит только то, что нужно для отображения, без логики измерения.
    // Представление состояния веса для вывода. Здесь только то, что нужно устройству
    // индикации, поэтому HUB75, LCD и лог могут использовать один контракт.
    struct DisplayFrame
    {
        struct Setpoint
        {
            const char *name = "";
            int64_t raw_value = 0;
        };

        const char *stage_name = "";
        int64_t raw_sum = 0;
        float weight = 0.0f;
        float target_weight = 0.0f;
        float remaining_weight = 0.0f;
        float remaining_shovels = 0.0f;
        std::array<Setpoint, domain::kMaxSetpoints> setpoints{};
        std::size_t setpoint_count = 0;
        uint32_t diagnostic_tick = 0;
        char wifi_state_code[4]{};
        char wifi_ip[16]{};
        bool wifi_ap_has_clients = false;
        bool valid = false;
    };

    // Граница для конкретных устройств вывода. Первая реализация пишет в лог;
    // будущий HUB75 должен подключаться здесь и не лезть в модуль измерения.
    class IDisplaySink
    {
    public:
        virtual ~IDisplaySink() = default;

        virtual esp_err_t begin() = 0;
        virtual void render(const DisplayFrame &frame) = 0;
    };

    // Минимальная реализация индикации для первичного запуска без матрицы. Она
    // проверяет контракт задачи индикации и не добавляет зависимость от железа.
    class LogDisplaySink final : public IDisplaySink
    {
    public:
        esp_err_t begin() override;
        void render(const DisplayFrame &frame) override;
    };

    // Реализация вывода на HUB75 64x64 через ESP32-HUB75-MatrixPanel-DMA. Она
    // получает уже подготовленный DisplayFrame и не знает, как читаются датчики или
    // где хранится калибровка.
    class Hub75DisplaySink final : public IDisplaySink
    {
    public:
        Hub75DisplaySink();
        ~Hub75DisplaySink() override;

        esp_err_t begin() override;
        void render(const DisplayFrame &frame) override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace mixer::display

namespace mixer::web { class WifiManager; }

namespace mixer::display {

    // Периодический рендерер, который превращает LatestWeightStore в DisplayFrame.
    // Он отделен от Web, чтобы визуальная индикация развивалась независимо.
    class DisplayTask
    {
    public:
        DisplayTask(processing::LatestWeightStore &latest,
                    settings::SettingsStore &settings,
                    web::WifiManager &wifi,
                    IDisplaySink &sink);

        esp_err_t start();

    private:
        static void taskEntry(void *context);
        void run();

        processing::LatestWeightStore &latest_;
        settings::SettingsStore &settings_;
        web::WifiManager &wifi_;
        IDisplaySink &sink_;
    };

} // пространство имен mixer::display
