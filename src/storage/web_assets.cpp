#include "storage/web_assets.hpp"

#include <cstddef>

#include "esp_log.h"
#include "esp_spiffs.h"

namespace mixer::storage {
namespace {

constexpr char kTag[] = "web_assets";
constexpr char kBasePath[] = "/www";
constexpr char kPartitionLabel[] = "www";

}  // анонимное пространство имен

esp_err_t WebAssets::mount() {
    if (mounted_) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t config{};
    config.base_path = kBasePath;
    config.partition_label = kPartitionLabel;
    config.max_files = 8;
    config.format_if_mount_failed = false;

    const esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    std::size_t total = 0;
    std::size_t used = 0;
    const esp_err_t info_err = esp_spiffs_info(kPartitionLabel, &total, &used);
    if (info_err == ESP_OK) {
        ESP_LOGI(kTag, "mounted %s: used %u of %u bytes",
                 kPartitionLabel,
                 static_cast<unsigned>(used),
                 static_cast<unsigned>(total));
    }

    mounted_ = true;
    return ESP_OK;
}

const char* WebAssets::basePath() const {
    return kBasePath;
}

}  // пространство имен mixer::storage
