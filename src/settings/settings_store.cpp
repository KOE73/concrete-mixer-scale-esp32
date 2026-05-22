#include "settings/settings_store.hpp"

#include <array>
#include <cstddef>
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
constexpr uint32_t kCalibrationVersion = 4;
constexpr char kWifiNamespace[] = "wifi";
constexpr char kWifiKey[] = "sta";
constexpr uint32_t kWifiVersion = 1;
constexpr char kUdpNamespace[] = "udp";
constexpr char kUdpKey[] = "telemetry";
constexpr uint32_t kUdpVersion = 1;

struct StoredCalibration {
    uint32_t version = kCalibrationVersion;
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

enum class BlobLoadStatus {
    Missing,
    Loaded,
    SchemaMismatch,
};

bool isSchemaMismatch(esp_err_t err) {
    return err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_NVS_TYPE_MISMATCH;
}

template <typename T>
esp_err_t loadBlob(const char* namespace_name,
                   const char* key,
                   T& value,
                   BlobLoadStatus& status,
                   std::size_t& stored_size) {
    status = BlobLoadStatus::Missing;
    stored_size = 0;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, key, nullptr, &stored_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (isSchemaMismatch(err)) {
        nvs_close(handle);
        status = BlobLoadStatus::SchemaMismatch;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    if (stored_size != sizeof(T)) {
        nvs_close(handle);
        status = BlobLoadStatus::SchemaMismatch;
        return ESP_OK;
    }

    std::size_t read_size = sizeof(T);
    err = nvs_get_blob(handle, key, &value, &read_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (isSchemaMismatch(err)) {
        status = BlobLoadStatus::SchemaMismatch;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (read_size != sizeof(T)) {
        status = BlobLoadStatus::SchemaMismatch;
        stored_size = read_size;
        return ESP_OK;
    }

    status = BlobLoadStatus::Loaded;
    return ESP_OK;
}

void eraseBlobIfPresent(const char* namespace_name, const char* key, const char* label) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "failed to open %s for erase: %s", label, esp_err_to_name(err));
        return;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGW(kTag, "erased invalid %s NVS entry", label);
    } else {
        ESP_LOGW(kTag, "failed to erase invalid %s NVS entry: %s", label, esp_err_to_name(err));
    }
}

domain::CalibrationState normalizeCalibration(const domain::CalibrationState& state) {
    domain::CalibrationState normalized = state;
    std::array<domain::UnitConversion, domain::kMaxUnitConversions> units{};
    std::array<domain::Setpoint, domain::kMaxSetpoints> setpoints{};
    uint8_t count = 0;

    for (const domain::UnitConversion& unit : state.units) {
        if (!unit.enabled || unit.name[0] == '\0' || unit.raw_per_unit <= 0.0f) {
            continue;
        }
        if (count >= domain::kMaxUnitConversions) {
            break;
        }

        domain::UnitConversion copy = unit;
        copy.enabled = true;
        copy.name[domain::kUnitNameMaxLength] = '\0';
        units[count++] = copy;
    }

    normalized.units = units;
    normalized.unit_count = count;

    count = 0;
    for (const domain::Setpoint& setpoint : state.setpoints) {
        if (!setpoint.enabled || setpoint.name[0] == '\0') {
            continue;
        }
        if (count >= domain::kMaxSetpoints) {
            break;
        }

        domain::Setpoint copy = setpoint;
        copy.enabled = true;
        copy.name[domain::kSetpointNameMaxLength] = '\0';
        setpoints[count++] = copy;
    }

    normalized.setpoints = setpoints;
    normalized.setpoint_count = count;
    return normalized;
}

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
    BlobLoadStatus status = BlobLoadStatus::Missing;
    std::size_t stored_size = 0;
    esp_err_t err = loadBlob(kCalibrationNamespace,
                             kCalibrationKey,
                             stored,
                             status,
                             stored_size);
    if (err != ESP_OK) {
        return err;
    }

    if (status == BlobLoadStatus::Missing) {
        ESP_LOGI(kTag, "calibration not found, using defaults");
        setCalibration(defaultCalibration());
    } else if (status == BlobLoadStatus::Loaded &&
               stored.version == kCalibrationVersion) {
        setCalibration(stored.state);
    } else {
        ESP_LOGW(kTag,
                 "calibration schema mismatch, using defaults (stored=%u expected=%u)",
                 static_cast<unsigned>(stored_size),
                 static_cast<unsigned>(sizeof(stored)));
        eraseBlobIfPresent(kCalibrationNamespace, kCalibrationKey, "calibration");
        setCalibration(defaultCalibration());
    }

    StoredWifi stored_wifi{};
    status = BlobLoadStatus::Missing;
    stored_size = 0;
    err = loadBlob(kWifiNamespace, kWifiKey, stored_wifi, status, stored_size);
    if (err != ESP_OK) {
        return err;
    }

    if (status == BlobLoadStatus::Missing) {
        ESP_LOGI(kTag, "wifi credentials not found, using defaults");
        setWifiCredentials(defaultWifiCredentials());
    } else if (status != BlobLoadStatus::Loaded ||
               stored_wifi.version != kWifiVersion) {
        ESP_LOGW(kTag,
                 "wifi schema mismatch, using defaults (stored=%u expected=%u)",
                 static_cast<unsigned>(stored_size),
                 static_cast<unsigned>(sizeof(stored_wifi)));
        eraseBlobIfPresent(kWifiNamespace, kWifiKey, "wifi");
        setWifiCredentials(defaultWifiCredentials());
    } else {
        setWifiCredentials(stored_wifi.credentials);
    }

    StoredUdpTelemetry stored_udp{};
    status = BlobLoadStatus::Missing;
    stored_size = 0;
    err = loadBlob(kUdpNamespace, kUdpKey, stored_udp, status, stored_size);
    if (err != ESP_OK) {
        return err;
    }

    if (status == BlobLoadStatus::Missing) {
        ESP_LOGI(kTag, "UDP telemetry settings not found, using defaults");
        setUdpTelemetry(defaultUdpTelemetry());
    } else if (status != BlobLoadStatus::Loaded ||
               stored_udp.version != kUdpVersion) {
        ESP_LOGW(kTag,
                 "UDP telemetry schema mismatch, using defaults (stored=%u expected=%u)",
                 static_cast<unsigned>(stored_size),
                 static_cast<unsigned>(sizeof(stored_udp)));
        eraseBlobIfPresent(kUdpNamespace, kUdpKey, "UDP telemetry");
        setUdpTelemetry(defaultUdpTelemetry());
    } else {
        setUdpTelemetry(stored_udp.settings);
    }

    return ESP_OK;
}

esp_err_t SettingsStore::save(const domain::CalibrationState& state) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kCalibrationNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    const domain::CalibrationState normalized = normalizeCalibration(state);
    StoredCalibration stored{};
    stored.state = normalized;

    err = nvs_set_blob(handle, kCalibrationKey, &stored, sizeof(stored));
    if (err != ESP_OK) {
        ESP_LOGW(kTag,
                 "calibration set failed, erasing old entry and retrying: %s",
                 esp_err_to_name(err));
        const esp_err_t erase_err = nvs_erase_key(handle, kCalibrationKey);
        if (erase_err == ESP_OK || erase_err == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_set_blob(handle, kCalibrationKey, &stored, sizeof(stored));
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        setCalibration(normalized);
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
    const domain::CalibrationState normalized = normalizeCalibration(state);
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        calibration_ = normalized;
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
    calibration.sum_offset = config::kDefaultSumOffset;
    calibration.sum_scale = config::kDefaultSumScale;
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
