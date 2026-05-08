#pragma once

#include "settings/settings_store.hpp"

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace mixer::web {

struct WifiStatus {
    bool ap_started = false;
    bool sta_configured = false;
    bool sta_connected = false;
    char ap_ssid[33]{};
    char sta_ssid[33]{};
    char sta_ip[16]{};
};

// Владеет обоими сетевыми режимами ESP32: постоянной сервисной точкой доступа
// и подключением к внешней Wi-Fi сети. AP нужен как гарантированный вход в UI,
// а STA нужен для нормальной работы в сети цеха/дома. Сохраненные через UI
// STA-учетные данные лежат в SettingsStore/NVS; прошитый wifi_secrets.hpp
// используется только как начальное значение для чистой платы.
class WifiManager {
public:
    explicit WifiManager(settings::SettingsStore& settings);
    ~WifiManager();

    esp_err_t start();
    esp_err_t connect(const settings::WifiCredentials& credentials);

    WifiStatus status() const;

private:
    static void eventHandler(void* arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void* event_data);

    void handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);
    void setStatus(const WifiStatus& status);

    settings::SettingsStore& settings_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    WifiStatus status_{};
};

}  // пространство имен mixer::web
