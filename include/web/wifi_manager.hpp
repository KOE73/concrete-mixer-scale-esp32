#pragma once

#include "settings/settings_store.hpp"

#include <vector>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace mixer::web {

enum class WifiState {
    Disabled,
    StaSearch,
    StaConnected,
    ApActive
};

struct WifiScanResult {
    char ssid[33]{};
    int8_t rssi = 0;
    uint8_t authmode = 0;
};

struct WifiStatus {
    WifiState state = WifiState::Disabled;
    bool ap_started = false;
    bool sta_configured = false;
    bool sta_connected = false;
    bool ap_has_clients = false;
    char ap_ssid[33]{};
    char sta_ssid[33]{};
    char sta_ip[16]{};
    char ap_mac[18]{};
    char sta_mac[18]{};
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
    std::vector<WifiScanResult> scanNetworks();

private:
    static void eventHandler(void* arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void* event_data);

    void handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);
    void setStatus(const WifiStatus& status);
    
    void executeTransition(WifiState next_state);
    esp_err_t startAccessPoint(bool include_sta);
    void handleWifiMaintenance();
    
    static void wifiTaskEntry(void* context);
    void runWifiLoop();
    void pollButton();
    void handleButtonClick();
    
    WifiState getWifiState() const;

    settings::SettingsStore& settings_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    WifiStatus status_{};
    
    WifiState pending_state_ = WifiState::Disabled;
    bool has_pending_state_ = false;
    
    std::size_t active_network_index_ = 0;
    uint32_t state_seconds_ = 0;
    uint32_t last_connect_attempt_seconds_ = 0;
    TaskHandle_t task_handle_ = nullptr;
    
    int debounce_counter_ = 0;
    bool was_pressed_ = false;
};

}  // пространство имен mixer::web
