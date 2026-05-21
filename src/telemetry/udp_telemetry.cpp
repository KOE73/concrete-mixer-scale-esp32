#include "telemetry/udp_telemetry.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "config/hardware_config.hpp"
#include "config/network_config.hpp"

#include "esp_log.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace mixer::telemetry {
namespace {

constexpr char kTag[] = "udp_telemetry";
constexpr std::size_t kPacketBufferSize = 512;

}  // namespace

UdpTelemetryTask::UdpTelemetryTask(processing::LatestWeightStore& latest,
                                   settings::SettingsStore& settings)
    : latest_(latest), settings_(settings) {}

esp_err_t UdpTelemetryTask::start() {
    const BaseType_t created = xTaskCreatePinnedToCore(
        &UdpTelemetryTask::taskEntry,
        "udp_telemetry",
        config::kUdpTelemetryTaskStackBytes,
        this,
        tskIDLE_PRIORITY + 1,
        nullptr,
        tskNO_AFFINITY);

    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void UdpTelemetryTask::taskEntry(void* context) {
    static_cast<UdpTelemetryTask*>(context)->run();
}

void UdpTelemetryTask::run() {
    int socket_fd = createSocket();

    while (true) {
        if (socket_fd < 0) {
            socket_fd = createSocket();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const domain::WeightState state = latest_.get();
        if (state.sample.sequence != 0 && state.sample.sequence != last_sequence_) {
            sendState(socket_fd, state);
            last_sequence_ = state.sample.sequence;

            if (send_failure_count_ >= 3) {
                close(socket_fd);
                socket_fd = -1;
                send_failure_count_ = 0;
                ESP_LOGW(kTag, "UDP socket recreated after repeated send failures");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(config::kSamplePeriodMs));
    }
}

int UdpTelemetryTask::createSocket() {
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(kTag, "socket create failed: errno=%d", errno);
        return -1;
    }

    const int yes = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0) {
        ESP_LOGW(kTag, "SO_BROADCAST failed: errno=%d", errno);
    }

    return socket_fd;
}

void UdpTelemetryTask::sendState(int socket_fd, const domain::WeightState& state) {
    const settings::UdpTelemetrySettings settings = settings_.udpTelemetry();
    if (!settings.enabled) {
        return;
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(settings.port);
    if (inet_aton(settings.target_host, &destination.sin_addr) == 0) {
        ESP_LOGW(kTag, "invalid UDP target host: %s", settings.target_host);
        return;
    }

    int64_t raw_sum = 0;
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        raw_sum += state.sample.raw[i];
    }

    char flags[48]{};
    std::snprintf(flags,
                  sizeof(flags),
                  "%s%s",
                  state.sample.valid ? "valid" : "invalid",
                  state.sample.valid ? "" : "|sensor_error");

    char packet[kPacketBufferSize]{};
    int written = std::snprintf(packet,
                                sizeof(packet),
                                "%lu,%llu,%llu",
                                static_cast<unsigned long>(settings.scale_id),
                                static_cast<unsigned long long>(state.sample.sequence),
                                static_cast<unsigned long long>(state.sample.timestamp_us / 1000));
    for (std::size_t i = 0; i < config::kLoadCellCount && written > 0 &&
                            static_cast<std::size_t>(written) < sizeof(packet);
         ++i) {
        written += std::snprintf(packet + written,
                                 sizeof(packet) - static_cast<std::size_t>(written),
                                 ",%ld",
                                 static_cast<long>(state.sample.raw[i]));
    }

    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(packet)) {
        return;
    }

    written += std::snprintf(packet + written,
                             sizeof(packet) - static_cast<std::size_t>(written),
                             ",%lld,%.3f,%s",
                             static_cast<long long>(raw_sum),
                             static_cast<double>(state.sample.weight),
                             flags);

    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(packet)) {
        ESP_LOGW(kTag, "UDP packet buffer too small");
        return;
    }

    for (std::size_t i = 0; i < state.filter_count && static_cast<std::size_t>(written) < sizeof(packet); ++i) {
        const char* name = state.filters[i].name;
        int win_sec = 0;
        if (std::sscanf(name, "ma_%ds", &win_sec) == 1) {
            written += std::snprintf(packet + written,
                                     sizeof(packet) - static_cast<std::size_t>(written),
                                     ",%d,%.3f",
                                     win_sec,
                                     static_cast<double>(state.filters[i].weight));
        }
    }

    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(packet)) {
        ESP_LOGW(kTag, "UDP packet buffer too small after appending filters");
        return;
    }

    const int sent = sendto(socket_fd,
                            packet,
                            static_cast<std::size_t>(written),
                            0,
                            reinterpret_cast<sockaddr*>(&destination),
                            sizeof(destination));
    if (sent < 0) {
        ++send_failure_count_;
        if (send_failure_count_ == 1 || send_failure_count_ % 10 == 0) {
            ESP_LOGW(kTag, "sendto failed: errno=%d failures=%lu",
                     errno,
                     static_cast<unsigned long>(send_failure_count_));
        }
        return;
    }

    if (send_failure_count_ != 0) {
        ESP_LOGI(kTag, "UDP send recovered after %lu failures",
                 static_cast<unsigned long>(send_failure_count_));
        send_failure_count_ = 0;
    }
    ++send_success_count_;
    if (send_success_count_ == 1 || send_success_count_ % 100 == 0) {
        ESP_LOGI(kTag,
                 "UDP sent seq=%llu to %s:%u successes=%lu",
                 static_cast<unsigned long long>(state.sample.sequence),
                 settings.target_host,
                 static_cast<unsigned>(settings.port),
                 static_cast<unsigned long>(send_success_count_));
    }
}

}  // namespace mixer::telemetry
