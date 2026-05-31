#include "web/wifi_manager.hpp"

#include <cstdio>
#include <cstring>

#include "config/network_config.hpp"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "driver/gpio.h"

namespace mixer::web {
namespace {

constexpr char kTag[] = "wifi";
constexpr gpio_num_t kButtonUpGpio = GPIO_NUM_6;

void copyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination_size == 0) {
        return;
    }
    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

void formatMac(char* destination, std::size_t destination_size, const uint8_t* mac) {
    if (destination_size == 0) {
        return;
    }
    snprintf(destination,
             destination_size,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
    destination[destination_size - 1] = '\0';
}

}  // анонимное пространство имен

namespace {

wifi_config_t makeApConfig() {
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
    return ap_config;
}

}  // namespace

WifiManager::WifiManager(settings::SettingsStore& settings)
    : settings_(settings), mutex_(xSemaphoreCreateMutex()) {}

WifiManager::~WifiManager() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
    }
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

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    // Инициализируем MAC-адреса в статусе.
    WifiStatus initial = status();
    uint8_t ap_mac[6]{};
    uint8_t sta_mac[6]{};
    if (esp_wifi_get_mac(WIFI_IF_AP, ap_mac) == ESP_OK) {
        formatMac(initial.ap_mac, sizeof(initial.ap_mac), ap_mac);
    }
    if (esp_wifi_get_mac(WIFI_IF_STA, sta_mac) == ESP_OK) {
        formatMac(initial.sta_mac, sizeof(initial.sta_mac), sta_mac);
    }
    setStatus(initial);

    // Запускаем асинхронную задачу управления Wi-Fi
    xTaskCreatePinnedToCore(
        &WifiManager::wifiTaskEntry,
        "wifi_manager_task",
        4096,
        this,
        tskIDLE_PRIORITY + 2,
        &task_handle_,
        tskNO_AFFINITY);

    // Стартуем в режиме поиска сетей (асинхронно через задачу)
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        pending_state_ = WifiState::StaSearch;
        has_pending_state_ = true;
        xSemaphoreGive(mutex_);
    }

    return ESP_OK;
}

esp_err_t WifiManager::connect(const settings::WifiCredentials& credentials) {
    // Вызывается при изменении настроек в Web UI.
    ESP_LOGI(kTag, "New credentials received, requesting STA search restart...");
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        pending_state_ = WifiState::StaSearch;
        has_pending_state_ = true;
        xSemaphoreGive(mutex_);
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
        const auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        next.sta_connected = false;
        next.sta_ip[0] = '\0';
        setStatus(next);
        ESP_LOGW(kTag,
                 "STA disconnected reason=%u state=%d heap=%u min_heap=%u largest=%u",
                 event != nullptr ? static_cast<unsigned>(event->reason) : 0U,
                 static_cast<int>(next.state),
                 static_cast<unsigned>(esp_get_free_heap_size()),
                 static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));

        // Если связь оборвалась в стабильном режиме STA, инициируем поиск заново через задачу
        if (next.state == WifiState::StaConnected) {
            ESP_LOGW(kTag, "STA link lost. Requesting transition to StaSearch.");
            if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
                pending_state_ = WifiState::StaSearch;
                has_pending_state_ = true;
                xSemaphoreGive(mutex_);
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (next.state != WifiState::StaSearch && next.state != WifiState::StaConnected) {
            ESP_LOGW(kTag, "Ignoring STA IP event while FSM state is %d", static_cast<int>(next.state));
            return;
        }

        const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        next.sta_connected = true;
        copyString(next.sta_ip,
                   sizeof(next.sta_ip),
                   ip4addr_ntoa(reinterpret_cast<const ip4_addr_t*>(&event->ip_info.ip)));
        setStatus(next);
        ESP_LOGI(kTag, "STA got IP %s", next.sta_ip);
        
        if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
            pending_state_ = WifiState::StaConnected;
            has_pending_state_ = true;
            xSemaphoreGive(mutex_);
        }
    }
}

void WifiManager::setStatus(const WifiStatus& status) {
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        status_ = status;
        xSemaphoreGive(mutex_);
    }
}

WifiState WifiManager::getWifiState() const {
    WifiState current = WifiState::Disabled;
    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        current = status_.state;
        xSemaphoreGive(mutex_);
    }
    return current;
}

void WifiManager::executeTransition(WifiState next_state) {
    WifiStatus next = status();
    next.state = next_state;
    state_seconds_ = 0;

    if (next_state == WifiState::Disabled) {
        ESP_LOGI(kTag, "FSM: Transition to Disabled (Wi-Fi turned off)");
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_NULL);

        next.sta_configured = false;
        next.sta_connected = false;
        next.sta_ssid[0] = '\0';
        next.sta_ip[0] = '\0';
        next.ap_started = false;
        next.ap_has_clients = false;
        next.ap_ssid[0] = '\0';
    }
    else if (next_state == WifiState::StaSearch) {
        ESP_LOGI(kTag, "FSM: Transition to StaSearch (scanning configured networks)");
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        esp_wifi_set_ps(WIFI_PS_NONE);

        const auto credentials = settings_.wifiCredentials();

        // Находим первую доступную для подключения сеть
        bool found = false;
        for (std::size_t i = 0; i < settings::kMaxWifiNetworks; ++i) {
            if (credentials.networks[i].ssid[0] != '\0') {
                active_network_index_ = i;
                found = true;
                break;
            }
        }

        next.sta_configured = credentials.configured && found;
        next.sta_connected = false;
        next.sta_ip[0] = '\0';
        next.ap_started = false;

        if (found) {
            copyString(next.sta_ssid, sizeof(next.sta_ssid), credentials.networks[active_network_index_].ssid);
            setStatus(next);

            wifi_config_t sta_config{};
            copyString(reinterpret_cast<char*>(sta_config.sta.ssid),
                       sizeof(sta_config.sta.ssid),
                       credentials.networks[active_network_index_].ssid);
            copyString(reinterpret_cast<char*>(sta_config.sta.password),
                       sizeof(sta_config.sta.password),
                       credentials.networks[active_network_index_].password);
            sta_config.sta.scan_method = WIFI_FAST_SCAN;
            sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            sta_config.sta.threshold.authmode =
                credentials.networks[active_network_index_].password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

            esp_wifi_set_config(WIFI_IF_STA, &sta_config);
            esp_wifi_connect();
            last_connect_attempt_seconds_ = 0;
        } else {
            next.sta_ssid[0] = '\0';
            setStatus(next);
            ESP_LOGW(kTag, "No saved networks configured. Transition to ApActive");
            executeTransition(WifiState::ApActive);
            return;
        }
    }
    else if (next_state == WifiState::StaConnected) {
        ESP_LOGI(kTag, "FSM: Transition to StaConnected");
        next.sta_connected = true;
        next.ap_started = false;
    }
    else if (next_state == WifiState::ApActive) {
        ESP_LOGI(kTag, "FSM: Transition to ApActive (AP-only, STA disconnected)");
        startAccessPoint(false);

        next.ap_started = true;
        next.ap_has_clients = false;
        next.sta_configured = false;
        next.sta_connected = false;
        next.sta_ssid[0] = '\0';
        next.sta_ip[0] = '\0';
        copyString(next.ap_ssid, sizeof(next.ap_ssid), config::kApSsid);
    }

    setStatus(next);
}

esp_err_t WifiManager::startAccessPoint(bool include_sta) {
    esp_wifi_disconnect();
    esp_wifi_stop();

    esp_err_t err = esp_wifi_set_mode(include_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(kTag,
                 "esp_wifi_set_mode(%s) failed: %s",
                 include_sta ? "APSTA" : "AP",
                 esp_err_to_name(err));
        return err;
    }

    wifi_config_t ap_config = makeApConfig();
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_start(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(kTag, "WiFi radio mode is %s", include_sta ? "APSTA for scan" : "AP only");
    return ESP_OK;
}

void WifiManager::handleWifiMaintenance() {
    WifiStatus current = status();
    if (current.state == WifiState::Disabled) {
        return;
    }

    state_seconds_++;

    if (current.state == WifiState::StaSearch) {
        last_connect_attempt_seconds_++;
        
        // STA search is intentionally endless: cycle saved networks until one connects.
        if (!current.sta_connected && last_connect_attempt_seconds_ >= 6) {
            const auto credentials = settings_.wifiCredentials();
            
            bool found = false;
            for (std::size_t i = 0; i < settings::kMaxWifiNetworks; ++i) {
                active_network_index_ = (active_network_index_ + 1) % settings::kMaxWifiNetworks;
                if (credentials.networks[active_network_index_].ssid[0] != '\0') {
                    found = true;
                    break;
                }
            }

            if (found) {
                ESP_LOGW(kTag, "Switching to next configured network '%s'", credentials.networks[active_network_index_].ssid);
                
                copyString(current.sta_ssid, sizeof(current.sta_ssid), credentials.networks[active_network_index_].ssid);
                setStatus(current);

                wifi_config_t sta_config{};
                copyString(reinterpret_cast<char*>(sta_config.sta.ssid),
                           sizeof(sta_config.sta.ssid),
                           credentials.networks[active_network_index_].ssid);
                copyString(reinterpret_cast<char*>(sta_config.sta.password),
                           sizeof(sta_config.sta.password),
                           credentials.networks[active_network_index_].password);
                sta_config.sta.scan_method = WIFI_FAST_SCAN;
                sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
                sta_config.sta.threshold.authmode =
                    credentials.networks[active_network_index_].password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

                esp_wifi_set_config(WIFI_IF_STA, &sta_config);
                esp_wifi_connect();
            }
            last_connect_attempt_seconds_ = 0;
        }
    }
    else if (current.state == WifiState::ApActive) {
        wifi_sta_list_t sta_list{};
        bool has_clients = false;
        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            has_clients = (sta_list.num > 0);
        }

        if (has_clients) {
            state_seconds_ = 0;
            if (!current.ap_has_clients) {
                current.ap_has_clients = true;
                setStatus(current);
                ESP_LOGI(kTag, "Client connected to AP.");
            }
        } else {
            if (current.ap_has_clients) {
                current.ap_has_clients = false;
                setStatus(current);
                ESP_LOGI(kTag, "Last client disconnected from AP.");
            }
        }
    }
}

void WifiManager::wifiTaskEntry(void* context) {
    static_cast<WifiManager*>(context)->runWifiLoop();
}

void WifiManager::runWifiLoop() {
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << kButtonUpGpio);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    debounce_counter_ = 0;
    was_pressed_ = false;

    uint32_t ticks = 0;

    while (true) {
        pollButton();

        ticks++;
        if (ticks >= 20) {
            ticks = 0;
            handleWifiMaintenance();
        }

        WifiState next_state = WifiState::Disabled;
        bool do_transition = false;
        
        if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (has_pending_state_) {
                next_state = pending_state_;
                has_pending_state_ = false;
                do_transition = true;
            }
            xSemaphoreGive(mutex_);
        }

        if (do_transition) {
            executeTransition(next_state);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void WifiManager::pollButton() {
    bool is_pressed = (gpio_get_level(kButtonUpGpio) == 0);
    if (is_pressed) {
        debounce_counter_++;
        if (debounce_counter_ >= 3) {
            if (!was_pressed_) {
                was_pressed_ = true;
                handleButtonClick();
            }
        }
    } else {
        debounce_counter_ = 0;
        was_pressed_ = false;
    }
}

void WifiManager::handleButtonClick() {
    WifiState state = getWifiState();
    ESP_LOGI(kTag, "BUTTON_UP click detected. Current FSM state: %d", static_cast<int>(state));

    WifiState target = WifiState::Disabled;
    if (state == WifiState::Disabled) {
        target = WifiState::StaSearch;
    } else if (state == WifiState::StaSearch || state == WifiState::StaConnected) {
        target = WifiState::ApActive;
    } else {
        target = WifiState::Disabled;
    }

    if (mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        pending_state_ = target;
        has_pending_state_ = true;
        xSemaphoreGive(mutex_);
    }
}

std::vector<WifiScanResult> WifiManager::scanNetworks() {
    std::vector<WifiScanResult> results;
    
    WifiState state = getWifiState();
    if (state == WifiState::Disabled) {
        ESP_LOGW(kTag, "Cannot scan WiFi: WiFi is disabled");
        return results;
    }

    bool restore_ap_only = state == WifiState::ApActive;
    if (restore_ap_only) {
        ESP_LOGI(kTag, "Temporarily enabling STA for WiFi scan");
        esp_err_t apsta_err = startAccessPoint(true);
        if (apsta_err != ESP_OK) {
            return results;
        }
    }

    auto restoreApOnly = [this, restore_ap_only]() {
        if (!restore_ap_only) {
            return;
        }
        esp_err_t restore_err = startAccessPoint(false);
        if (restore_err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to restore AP-only mode after scan: %s", esp_err_to_name(restore_err));
        }
    };

    wifi_scan_config_t scan_config{};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    
    ESP_LOGI(kTag, "Starting WiFi scan...");
    esp_err_t err = esp_wifi_scan_start(&scan_config, true); // Блокирующий вызов
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        restoreApOnly();
        return results;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num == 0) {
        ESP_LOGI(kTag, "No APs found during scan");
        restoreApOnly();
        return results;
    }

    if (ap_num > 20) {
        ap_num = 20; // Ограничим список 20-ю сетями
    }

    std::vector<wifi_ap_record_t> ap_records(ap_num);
    err = esp_wifi_scan_get_ap_records(&ap_num, ap_records.data());
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(err));
        restoreApOnly();
        return results;
    }

    results.reserve(ap_num);
    for (uint16_t i = 0; i < ap_num; ++i) {
        if (ap_records[i].ssid[0] == '\0') {
            continue;
        }
        
        bool duplicate = false;
        for (const auto& res : results) {
            if (std::strcmp(res.ssid, reinterpret_cast<const char*>(ap_records[i].ssid)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        WifiScanResult res{};
        std::strncpy(res.ssid, reinterpret_cast<const char*>(ap_records[i].ssid), sizeof(res.ssid) - 1);
        res.rssi = ap_records[i].rssi;
        res.authmode = ap_records[i].authmode;
        results.push_back(res);
    }

    ESP_LOGI(kTag, "WiFi scan finished, found %d unique APs", (int)results.size());
    restoreApOnly();
    return results;
}

}  // пространство имен mixer::web
