#pragma once

#include "processing/weight_processor.hpp"
#include "settings/settings_store.hpp"
#include "storage/web_assets.hpp"
#include "web/wifi_manager.hpp"

#include "esp_err.h"
#include "esp_http_server.h"

namespace mixer::web {

// Встроенный HTTP UI/API для текущего веса, обновления калибровки, настройки
// Wi-Fi и раздачи статических файлов из SPIFFS. Он намеренно не трогает HX711
// напрямую: данные измерения берет из LatestWeightStore, постоянные настройки
// пишет через SettingsStore, а переподключение к сети делегирует WifiManager.
class WebServer {
public:
    WebServer(processing::LatestWeightStore& latest,
              settings::SettingsStore& settings,
              storage::WebAssets& assets,
              WifiManager& wifi);

    esp_err_t start();
    void stop();

private:
    static esp_err_t staticFileHandler(httpd_req_t* req);
    static esp_err_t weightHandler(httpd_req_t* req);
    static esp_err_t settingsGetHandler(httpd_req_t* req);
    static esp_err_t settingsPostHandler(httpd_req_t* req);
    static esp_err_t wifiGetHandler(httpd_req_t* req);
    static esp_err_t wifiPostHandler(httpd_req_t* req);

    esp_err_t sendWeight(httpd_req_t* req) const;
    esp_err_t sendSettings(httpd_req_t* req) const;
    esp_err_t updateSettings(httpd_req_t* req);
    esp_err_t sendWifi(httpd_req_t* req) const;
    esp_err_t updateWifi(httpd_req_t* req);
    esp_err_t sendStaticFile(httpd_req_t* req) const;

    processing::LatestWeightStore& latest_;
    settings::SettingsStore& settings_;
    storage::WebAssets& assets_;
    WifiManager& wifi_;
    httpd_handle_t server_ = nullptr;
};

}  // пространство имен mixer::web
