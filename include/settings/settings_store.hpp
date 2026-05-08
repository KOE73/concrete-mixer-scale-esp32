#pragma once

#include "domain/weight_types.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace mixer::settings {

struct WifiCredentials {
    bool configured = false;
    char ssid[33]{};
    char password[65]{};
};

// Владеет постоянными настройками в NVS и отдает mutex-защищенные копии
// рабочим модулям. Калибровку читает сэмплер, Wi-Fi читает сетевой менеджер;
// Web меняет оба набора настроек через один store, чтобы правила хранения
// оставались в одном месте и не расползались по HTTP-обработчикам.
class SettingsStore {
public:
    SettingsStore();
    ~SettingsStore();

    esp_err_t load();
    esp_err_t save(const domain::CalibrationState& state);
    esp_err_t saveWifi(const WifiCredentials& credentials);

    domain::CalibrationState calibration() const;
    void setCalibration(const domain::CalibrationState& state);
    WifiCredentials wifiCredentials() const;
    void setWifiCredentials(const WifiCredentials& credentials);

private:
    static domain::CalibrationState defaultCalibration();
    static WifiCredentials defaultWifiCredentials();

    mutable SemaphoreHandle_t mutex_ = nullptr;
    domain::CalibrationState calibration_{};
    WifiCredentials wifi_{};
};

}  // пространство имен mixer::settings
