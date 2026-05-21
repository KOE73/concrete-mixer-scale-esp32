#include "settings/settings_store.hpp"

#include <cstring>

#include "config/hardware_config.hpp"
#include "config/network_config.hpp"

#include "esp_log.h"
#include "nvs.h"

namespace mixer::settings {
namespace {

constexpr char kTag[] = "settings";
constexpr char kCalibrationNamespace[] = "calibration";
constexpr char kCalibrationKey[] = "state";
constexpr uint32_t kCalibrationVersion = 1;
constexpr char kWifiNamespace[] = "wifi";
constexpr char kWifiKey[] = "sta";
constexpr uint32_t kWifiVersion = 1;
constexpr char kUdpNamespace[] = "udp";
constexpr char kUdpKey[] = "telemetry";
constexpr uint32_t kUdpVersion = 1;

struct StoredCalibration {
    uint32_t version = kCalibrationVersion;
    uint32_t channel_count = config::kLoadCellCount;
    domain::CalibrationState state{};
};

struct StoredWifi {
    uint32_t version = kWifiVersion;
    WifiCredentials credentials{};
};

struct StoredUdpTelemetry {
    uint32_t version = kUdpVersion;
    UdpTelemetrySettings settings{};
};

}  // анонимное пространство имен

SettingsStore::SettingsStore()
    : mutex_(xSemaphoreCreateMutex()),
      calibration_(defaultCalibration()),
      wifi_(defaultWifiCredentials()),
      udp_telemetry_(defaultUdpTelemetry()) {}

SettingsStore::~SettingsStore() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

esp_err_t SettingsStore::load() {
    StoredCalibration stored{};
    std::size_t size = sizeof(stored);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kCalibrationNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "calibration not found, using defaults");
        setCalibration(defaultCalibration());
    } else if (err != ESP_OK) {
        return err;
    } else {
        err = nvs_get_blob(handle, kCalibrationKey, &stored, &size);
        nvs_close(handle);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(kTag, "calibration blob not found, using defaults");
            setCalibration(defaultCalibration());
        } else if (err != ESP_OK) {
            return err;
        } else if (size != sizeof(stored) || stored.version != kCalibrationVersion ||
                   stored.channel_count != config::kLoadCellCount) {
            ESP_LOGW(kTag, "calibration schema mismatch, using defaults");
            setCalibration(defaultCalibration());
        } else {
            setCalibration(stored.state);
        }
    }

    StoredWifi stored_wifi{};
    size = sizeof(stored_wifi);
    err = nvs_open(kWifiNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "wifi credentials not found, using defaults");
        setWifiCredentials(defaultWifiCredentials());
        setUdpTelemetry(defaultUdpTelemetry());
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, kWifiKey, &stored_wifi, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "wifi blob not found, using defaults");
        setWifiCredentials(defaultWifiCredentials());
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(stored_wifi) || stored_wifi.version != kWifiVersion) {
        ESP_LOGW(kTag, "wifi schema mismatch, using defaults");
        setWifiCredentials(defaultWifiCredentials());
        return ESP_OK;
    }

    setWifiCredentials(stored_wifi.credentials);

    StoredUdpTelemetry stored_udp{};
    size = sizeof(stored_udp);
    err = nvs_open(kUdpNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "UDP telemetry settings not found, using defaults");
        setUdpTelemetry(defaultUdpTelemetry());
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, kUdpKey, &stored_udp, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "UDP telemetry blob not found, using defaults");
        setUdpTelemetry(defaultUdpTelemetry());
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(stored_udp) || stored_udp.version != kUdpVersion) {
        ESP_LOGW(kTag, "UDP telemetry schema mismatch, using defaults");
        setUdpTelemetry(defaultUdpTelemetry());
        return ESP_OK;
    }

    setUdpTelemetry(stored_udp.settings);
    return ESP_OK;
}

esp_err_t SettingsStore::save(const domain::CalibrationState& state) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kCalibrationNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    StoredCalibration stored{};
    stored.state = state;

    err = nvs_set_blob(handle, kCalibrationKey, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        setCalibration(state);
    }
    return err;
}

esp_err_t SettingsStore::saveWifi(const WifiCredentials& credentials) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kWifiNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    StoredWifi stored{};
    stored.credentials = credentials;
    stored.credentials.ssid[sizeof(stored.credentials.ssid) - 1] = '\0';
    stored.credentials.password[sizeof(stored.credentials.password) - 1] = '\0';
    stored.credentials.configured = stored.credentials.ssid[0] != '\0';

    err = nvs_set_blob(handle, kWifiKey, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        setWifiCredentials(stored.credentials);
    }
    return err;
}

esp_err_t SettingsStore::saveUdpTelemetry(const UdpTelemetrySettings& settings) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kUdpNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    StoredUdpTelemetry stored{};
    stored.settings = settings;
    stored.settings.target_host[sizeof(stored.settings.target_host) - 1] = '\0';
    if (stored.settings.scale_id == 0) {
        stored.settings.scale_id = config::kDefaultScaleId;
    }
    if (stored.settings.port == 0) {
        stored.settings.port = config::kDefaultUdpTelemetryPort;
    }
    if (stored.settings.target_host[0] == '\0') {
        std::strncpy(stored.settings.target_host,
                     config::kDefaultUdpTelemetryTargetHost,
                     sizeof(stored.settings.target_host) - 1);
    }

    err = nvs_set_blob(handle, kUdpKey, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        setUdpTelemetry(stored.settings);
    }
    return err;
}

domain::CalibrationState SettingsStore::calibration() const {
    domain::CalibrationState copy{};
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        copy = calibration_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

WifiCredentials SettingsStore::wifiCredentials() const {
    WifiCredentials copy{};
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        copy = wifi_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

UdpTelemetrySettings SettingsStore::udpTelemetry() const {
    UdpTelemetrySettings copy{};
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        copy = udp_telemetry_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

void SettingsStore::setCalibration(const domain::CalibrationState& state) {
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        calibration_ = state;
        xSemaphoreGive(mutex_);
    }
}

void SettingsStore::setWifiCredentials(const WifiCredentials& credentials) {
    WifiCredentials normalized = credentials;
    normalized.ssid[sizeof(normalized.ssid) - 1] = '\0';
    normalized.password[sizeof(normalized.password) - 1] = '\0';
    normalized.configured = normalized.ssid[0] != '\0';

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        wifi_ = normalized;
        xSemaphoreGive(mutex_);
    }
}

void SettingsStore::setUdpTelemetry(const UdpTelemetrySettings& settings) {
    UdpTelemetrySettings normalized = settings;
    normalized.target_host[sizeof(normalized.target_host) - 1] = '\0';
    if (normalized.scale_id == 0) {
        normalized.scale_id = config::kDefaultScaleId;
    }
    if (normalized.port == 0) {
        normalized.port = config::kDefaultUdpTelemetryPort;
    }
    if (normalized.target_host[0] == '\0') {
        std::strncpy(normalized.target_host,
                     config::kDefaultUdpTelemetryTargetHost,
                     sizeof(normalized.target_host) - 1);
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        udp_telemetry_ = normalized;
        xSemaphoreGive(mutex_);
    }
}

domain::CalibrationState SettingsStore::defaultCalibration() {
    domain::CalibrationState calibration{};
    calibration.global_scale = config::kDefaultGlobalScale;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        calibration.offsets[i] = config::kLoadCells[i].offset;
        calibration.scales[i] = config::kLoadCells[i].scale;
    }
    return calibration;
}

WifiCredentials SettingsStore::defaultWifiCredentials() {
    WifiCredentials credentials{};
    std::strncpy(credentials.ssid, config::kDefaultStaSsid, sizeof(credentials.ssid) - 1);
    std::strncpy(credentials.password,
                 config::kDefaultStaPassword,
                 sizeof(credentials.password) - 1);
    credentials.configured = credentials.ssid[0] != '\0';
    return credentials;
}

UdpTelemetrySettings SettingsStore::defaultUdpTelemetry() {
    UdpTelemetrySettings settings{};
    settings.enabled = config::kDefaultUdpTelemetryEnabled;
    settings.scale_id = config::kDefaultScaleId;
    settings.port = config::kDefaultUdpTelemetryPort;
    std::strncpy(settings.target_host,
                 config::kDefaultUdpTelemetryTargetHost,
                 sizeof(settings.target_host) - 1);
    return settings;
}

}  // пространство имен mixer::settings
