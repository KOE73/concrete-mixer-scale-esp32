#include "web/wifi_manager.hpp"

#include <cstring>

#include "config/network_config.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

namespace mixer::web {
namespace {

constexpr char kTag[] = "wifi";

void copyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination_size == 0) {
        return;
    }
    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

}  // анонимное пространство имен

WifiManager::WifiManager(settings::SettingsStore& settings)
    : settings_(settings), mutex_(xSemaphoreCreateMutex()) {}

WifiManager::~WifiManager() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

esp_err_t WifiManager::start() {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::eventHandler, this, nullptr);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::eventHandler, this, nullptr);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap_config{};
    copyString(reinterpret_cast<char*>(ap_config.ap.ssid),
               sizeof(ap_config.ap.ssid),
               config::kApSsid);
    copyString(reinterpret_cast<char*>(ap_config.ap.password),
               sizeof(ap_config.ap.password),
               config::kApPassword);
    ap_config.ap.ssid_len = std::strlen(config::kApSsid);
    ap_config.ap.channel = config::kApChannel;
    ap_config.ap.max_connection = config::kApMaxConnections;
    ap_config.ap.authmode =
        std::strlen(config::kApPassword) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    WifiStatus next = status();
    next.ap_started = true;
    copyString(next.ap_ssid, sizeof(next.ap_ssid), config::kApSsid);
    setStatus(next);
    ESP_LOGI(kTag, "AP '%s' started", config::kApSsid);

    const settings::WifiCredentials credentials = settings_.wifiCredentials();
    if (credentials.configured) {
        return connect(credentials);
    }

    ESP_LOGI(kTag, "STA credentials are empty, AP-only access remains active");
    return ESP_OK;
}

esp_err_t WifiManager::connect(const settings::WifiCredentials& credentials) {
    WifiStatus next = status();
    next.sta_configured = credentials.configured && credentials.ssid[0] != '\0';
    next.sta_connected = false;
    next.sta_ip[0] = '\0';
    copyString(next.sta_ssid, sizeof(next.sta_ssid), credentials.ssid);
    setStatus(next);

    esp_wifi_disconnect();

    if (!next.sta_configured) {
        ESP_LOGI(kTag, "STA disabled because SSID is empty");
        return ESP_OK;
    }

    wifi_config_t sta_config{};
    copyString(reinterpret_cast<char*>(sta_config.sta.ssid),
               sizeof(sta_config.sta.ssid),
               credentials.ssid);
    copyString(reinterpret_cast<char*>(sta_config.sta.password),
               sizeof(sta_config.sta.password),
               credentials.password);
    sta_config.sta.scan_method = WIFI_FAST_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_config.sta.threshold.authmode =
        credentials.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(kTag, "connecting STA to '%s'", credentials.ssid);
    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        return err;
    }
    return ESP_OK;
}

WifiStatus WifiManager::status() const {
    WifiStatus copy{};
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        copy = status_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

void WifiManager::eventHandler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data) {
    static_cast<WifiManager*>(arg)->handleEvent(event_base, event_id, event_data);
}

void WifiManager::handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WifiStatus next = status();

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        next.sta_connected = false;
        next.sta_ip[0] = '\0';
        setStatus(next);

        const settings::WifiCredentials credentials = settings_.wifiCredentials();
        if (credentials.configured) {
            ESP_LOGW(kTag, "STA disconnected, reconnecting to '%s'", credentials.ssid);
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        next.sta_connected = true;
        copyString(next.sta_ip,
                   sizeof(next.sta_ip),
                   ip4addr_ntoa(reinterpret_cast<const ip4_addr_t*>(&event->ip_info.ip)));
        setStatus(next);
        ESP_LOGI(kTag, "STA got IP %s", next.sta_ip);
    }
}

void WifiManager::setStatus(const WifiStatus& status) {
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        status_ = status;
        xSemaphoreGive(mutex_);
    }
}

}  // пространство имен mixer::web
