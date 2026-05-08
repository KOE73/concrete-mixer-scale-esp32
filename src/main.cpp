#include "config/hardware_config.hpp"
#include "display/display.hpp"
#include "measurement/load_cell_sampler.hpp"
#include "processing/weight_processor.hpp"
#include "settings/settings_store.hpp"
#include "storage/web_assets.hpp"
#include "web/web_server.hpp"
#include "web/wifi_manager.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

#include <cstdlib>

namespace {

constexpr char kTag[] = "main";

void requireOk(esp_err_t err, const char* step) {
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "%s failed: %s", step, esp_err_to_name(err));
        abort();
    }
}

esp_err_t initializeNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        requireOk(nvs_flash_erase(), "nvs_flash_erase");
        err = nvs_flash_init();
    }
    return err;
}

}  // анонимное пространство имен

extern "C" void app_main() {
    requireOk(initializeNvs(), "nvs_flash_init");

    static QueueHandle_t sample_queue =
        xQueueCreate(4, sizeof(mixer::domain::WeightSample));
    requireOk(sample_queue == nullptr ? ESP_ERR_NO_MEM : ESP_OK, "sample queue create");

    static mixer::settings::SettingsStore settings;
    requireOk(settings.load(), "settings load");

    static mixer::processing::LatestWeightStore latest;
    static mixer::measurement::LoadCellSampler sampler(settings, sample_queue);
    static mixer::processing::WeightProcessor processor(sample_queue, latest);
    static mixer::display::Hub75DisplaySink hub75_display_sink;
    static mixer::display::LogDisplaySink log_display_sink;
    mixer::display::IDisplaySink& display_sink =
        mixer::config::kDisplayDriver == mixer::config::DisplayDriver::Hub75
            ? static_cast<mixer::display::IDisplaySink&>(hub75_display_sink)
            : static_cast<mixer::display::IDisplaySink&>(log_display_sink);
    static mixer::display::DisplayTask display(latest, display_sink);
    static mixer::storage::WebAssets web_assets;
    static mixer::web::WifiManager wifi(settings);
    static mixer::web::WebServer web(latest, settings, web_assets, wifi);

    requireOk(sampler.initialize(), "sampler initialize");
    requireOk(processor.start(), "processor start");
    requireOk(sampler.start(), "sampler start");
    requireOk(display.start(), "display start");
    requireOk(web_assets.mount(), "web assets mount");
    requireOk(wifi.start(), "wifi start");
    requireOk(web.start(), "web start");

    ESP_LOGI(kTag, "concrete mixer scale firmware started");
}
