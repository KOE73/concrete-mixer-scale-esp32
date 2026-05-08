#include "display/display.hpp"

#include "config/hardware_config.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace mixer::display {
namespace {

constexpr char kTag[] = "display";

}  // анонимное пространство имен

esp_err_t LogDisplaySink::begin() {
    ESP_LOGI(kTag, "log display sink started; replace IDisplaySink for HUB75/LCD output");
    return ESP_OK;
}

void LogDisplaySink::render(const DisplayFrame& frame) {
    if (!frame.valid) {
        ESP_LOGI(kTag, "weight: no valid sample yet");
        return;
    }

    ESP_LOGI(kTag, "%s: weight %.2f / target %.2f / remaining %.2f / shovels %.1f",
             frame.stage_name,
             frame.weight,
             frame.target_weight,
             frame.remaining_weight,
             frame.remaining_shovels);
}

DisplayTask::DisplayTask(processing::LatestWeightStore& latest, IDisplaySink& sink)
    : latest_(latest), sink_(sink) {}

esp_err_t DisplayTask::start() {
    esp_err_t err = sink_.begin();
    if (err != ESP_OK) {
        return err;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        &DisplayTask::taskEntry,
        "display_task",
        config::kDisplayTaskStackBytes,
        this,
        tskIDLE_PRIORITY + 1,
        nullptr,
        tskNO_AFFINITY);

    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void DisplayTask::taskEntry(void* context) {
    static_cast<DisplayTask*>(context)->run();
}

void DisplayTask::run() {
    uint32_t diagnostic_tick = 0;

    while (true) {
        const domain::WeightState state = latest_.get();
        const domain::FilterOutput primary =
            state.filter_count > 1 ? state.filters[1] : state.filters[0];

        DisplayFrame frame{};
        frame.stage_name = config::kDefaultBatchStageName;
        frame.weight = primary.weight;
        frame.target_weight = config::kDefaultBatchTargetWeight;
        frame.remaining_weight = frame.target_weight - frame.weight;
        frame.remaining_shovels =
            config::kDefaultShovelWeight > 0.0f
                ? frame.remaining_weight / config::kDefaultShovelWeight
                : 0.0f;
        frame.diagnostic_tick = diagnostic_tick++;
        frame.valid = primary.valid;

        sink_.render(frame);
        vTaskDelay(pdMS_TO_TICKS(config::kDisplayRefreshPeriodMs));
    }
}

}  // пространство имен mixer::display
