#pragma once

#include <array>

#include "domain/weight_types.hpp"
#include "measurement/load_cell_reader.hpp"
#include "settings/settings_store.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace mixer::measurement {

// Периодический производитель логических замеров веса. Он просит LoadCellReader
// прочитать raw-значения, применяет текущую калибровку из SettingsStore и
// публикует результат в очередь FreeRTOS для WeightProcessor.
class LoadCellSampler {
public:
    LoadCellSampler(settings::SettingsStore& settings, QueueHandle_t output_queue);

    esp_err_t initialize();
    esp_err_t start();

private:
    static void taskEntry(void* context);
    void run();
    domain::WeightSample readSample();

    settings::SettingsStore& settings_;
    QueueHandle_t output_queue_ = nullptr;
    LoadCellReader reader_{};
    uint64_t sequence_ = 0;
};

}  // пространство имен mixer::measurement
