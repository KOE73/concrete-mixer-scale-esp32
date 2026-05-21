#pragma once

#include "processing/weight_processor.hpp"
#include "settings/settings_store.hpp"

#include "esp_err.h"

namespace mixer::telemetry {

// Отправляет последний обработанный замер наружу в CSV over UDP. Данные берет
// только из LatestWeightStore, чтобы телеметрия не зависела от HX711 и очередей.
class UdpTelemetryTask {
public:
    UdpTelemetryTask(processing::LatestWeightStore& latest, settings::SettingsStore& settings);

    esp_err_t start();

private:
    static void taskEntry(void* context);
    void run();
    static int createSocket();
    void sendState(int socket_fd, const domain::WeightState& state);

    processing::LatestWeightStore& latest_;
    settings::SettingsStore& settings_;
    uint64_t last_sequence_ = 0;
    uint32_t send_failure_count_ = 0;
    uint32_t send_success_count_ = 0;
};

}  // namespace mixer::telemetry
