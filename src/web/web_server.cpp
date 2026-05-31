#include "web/web_server.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

#include "cJSON.h"
#include "config/hardware_config.hpp"
#include "esp_log.h"
#include "esp_wifi.h"

namespace mixer::web {
namespace {

constexpr char kTag[] = "web";
constexpr std::size_t kMaxPostBodyBytes = 4096;
constexpr std::size_t kFileBufferSize = 1024;
constexpr std::size_t kCborBufferSize = 2048;
constexpr std::size_t kMaxUriHandlers = 12;

using BodyBuffer = std::unique_ptr<char, decltype(&std::free)>;

void setJsonHeaders(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

const char* contentTypeForPath(const char* path) {
    const char* extension = std::strrchr(path, '.');
    if (extension == nullptr) {
        return "application/octet-stream";
    }
    if (std::strcmp(extension, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (std::strcmp(extension, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (std::strcmp(extension, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (std::strcmp(extension, ".json") == 0) {
        return "application/json";
    }
    return "application/octet-stream";
}

bool isEnglishLettersOnly(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }

    for (const char* p = value; *p != '\0'; ++p) {
        const char ch = *p;
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) {
            return false;
        }
    }
    return true;
}

esp_err_t readJsonBody(httpd_req_t* req, BodyBuffer& body) {
    if (req->content_len >= kMaxPostBodyBytes) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    }

    body.reset(static_cast<char*>(std::calloc(req->content_len + 1, 1)));
    if (body == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body allocation failed");
    }

    std::size_t received = 0;
    while (received < req->content_len) {
        const int read = httpd_req_recv(req, body.get() + received, req->content_len - received);
        if (read == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (read <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
        }
        received += static_cast<std::size_t>(read);
    }

    body.get()[received] = '\0';
    return ESP_OK;
}

bool customUriMatcher(const char* template_uri, const char* uri, size_t uri_len) {
    const char* path = uri;
    if (std::strncmp(uri, "http://", 7) == 0) {
        path = std::strchr(uri + 7, '/');
    } else if (std::strncmp(uri, "https://", 8) == 0) {
        path = std::strchr(uri + 8, '/');
    }
    
    if (path == nullptr) {
        path = "/";
    }
    
    // В ESP-IDF при матчинге с query-параметрами (например, "/api/state.cbor?_=123")
    // нужно обрезать query-параметры из пути перед сравнением с точным шаблоном.
    char clean_path[128]{};
    std::strncpy(clean_path, path, sizeof(clean_path) - 1);
    char* query_start = std::strchr(clean_path, '?');
    if (query_start != nullptr) {
        *query_start = '\0';
    }
    
    return httpd_uri_match_wildcard(template_uri, clean_path, std::strlen(clean_path));
}

}  // анонимное пространство имен

WebServer::WebServer(processing::LatestWeightStore& latest,
                     settings::SettingsStore& settings,
                     storage::WebAssets& assets,
                     WifiManager& wifi)
    : latest_(latest), settings_(settings), assets_(assets), wifi_(wifi) {}

esp_err_t WebServer::start() {
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.stack_size = config::kWebServerTaskStackBytes;
    http_config.max_open_sockets = 10;
    http_config.backlog_conn = 8;
    http_config.lru_purge_enable = true;
    http_config.recv_wait_timeout = 2;
    http_config.send_wait_timeout = 2;
    http_config.max_uri_handlers = kMaxUriHandlers;
    http_config.uri_match_fn = customUriMatcher;

    esp_err_t err = httpd_start(&server_, &http_config);
    if (err != ESP_OK) {
        return err;
    }

    const auto registerUri = [this](const httpd_uri_t& uri) -> esp_err_t {
        const esp_err_t err = httpd_register_uri_handler(server_, &uri);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "failed to register HTTP route %s: %s",
                     uri.uri,
                     esp_err_to_name(err));
            httpd_stop(server_);
            server_ = nullptr;
        }
        return err;
    };

    httpd_uri_t state_cbor{};
    state_cbor.uri = "/api/state.cbor";
    state_cbor.method = HTTP_GET;
    state_cbor.handler = &WebServer::stateCborHandler;
    state_cbor.user_ctx = this;
    err = registerUri(state_cbor);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t settings_get{};
    settings_get.uri = "/api/settings";
    settings_get.method = HTTP_GET;
    settings_get.handler = &WebServer::settingsGetHandler;
    settings_get.user_ctx = this;
    err = registerUri(settings_get);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t settings_post{};
    settings_post.uri = "/api/settings";
    settings_post.method = HTTP_POST;
    settings_post.handler = &WebServer::settingsPostHandler;
    settings_post.user_ctx = this;
    err = registerUri(settings_post);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t wifi_get{};
    wifi_get.uri = "/api/wifi";
    wifi_get.method = HTTP_GET;
    wifi_get.handler = &WebServer::wifiGetHandler;
    wifi_get.user_ctx = this;
    err = registerUri(wifi_get);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t wifi_post{};
    wifi_post.uri = "/api/wifi";
    wifi_post.method = HTTP_POST;
    wifi_post.handler = &WebServer::wifiPostHandler;
    wifi_post.user_ctx = this;
    err = registerUri(wifi_post);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t wifi_scan{};
    wifi_scan.uri = "/api/wifi-scan";
    wifi_scan.method = HTTP_GET;
    wifi_scan.handler = &WebServer::wifiScanHandler;
    wifi_scan.user_ctx = this;
    err = registerUri(wifi_scan);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t udp_get{};
    udp_get.uri = "/api/udp-telemetry";
    udp_get.method = HTTP_GET;
    udp_get.handler = &WebServer::udpTelemetryGetHandler;
    udp_get.user_ctx = this;
    err = registerUri(udp_get);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t udp_post{};
    udp_post.uri = "/api/udp-telemetry";
    udp_post.method = HTTP_POST;
    udp_post.handler = &WebServer::udpTelemetryPostHandler;
    udp_post.user_ctx = this;
    err = registerUri(udp_post);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root_uri{};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = &WebServer::staticFileHandler;
    root_uri.user_ctx = this;
    err = registerUri(root_uri);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t index_uri{};
    index_uri.uri = "/index.html";
    index_uri.method = HTTP_GET;
    index_uri.handler = &WebServer::staticFileHandler;
    index_uri.user_ctx = this;
    err = registerUri(index_uri);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t static_files{};
    static_files.uri = "/*";
    static_files.method = HTTP_GET;
    static_files.handler = &WebServer::staticFileHandler;
    static_files.user_ctx = this;
    err = registerUri(static_files);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(kTag, "HTTP server started");
    return ESP_OK;
}

void WebServer::stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

esp_err_t WebServer::staticFileHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendStaticFile(req);
}

esp_err_t WebServer::stateCborHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendStateCbor(req);
}

esp_err_t WebServer::settingsGetHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendSettings(req);
}

esp_err_t WebServer::settingsPostHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->updateSettings(req);
}

esp_err_t WebServer::wifiGetHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendWifi(req);
}

esp_err_t WebServer::wifiPostHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->updateWifi(req);
}

esp_err_t WebServer::wifiScanHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendWifiScan(req);
}

class CborWriter {
public:
    CborWriter(uint8_t* data, std::size_t capacity) : data_(data), capacity_(capacity) {}

    bool ok() const { return ok_; }
    const uint8_t* data() const { return data_; }
    std::size_t size() const { return pos_; }

    void map(std::size_t count) { typeValue(5, count); }
    void array(std::size_t count) { typeValue(4, count); }
    void key(const char* value) { text(value); }
    void text(const char* value) {
        const std::size_t len = value == nullptr ? 0 : std::strlen(value);
        typeValue(3, len);
        bytes(reinterpret_cast<const uint8_t*>(value == nullptr ? "" : value), len);
    }
    void boolean(bool value) { byte(value ? 0xF5 : 0xF4); }
    void u64(uint64_t value) { typeValue(0, value); }
    void i64(int64_t value) {
        if (value >= 0) {
            typeValue(0, static_cast<uint64_t>(value));
        } else {
            typeValue(1, static_cast<uint64_t>(-1 - value));
        }
    }
    void f64(double value) {
        byte(0xFB);
        static_assert(sizeof(double) == sizeof(uint64_t));
        uint64_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        for (int shift = 56; shift >= 0; shift -= 8) {
            byte(static_cast<uint8_t>((raw >> shift) & 0xFF));
        }
    }

private:
    void typeValue(uint8_t major, uint64_t value) {
        if (value <= 23) {
            byte(static_cast<uint8_t>((major << 5) | value));
        } else if (value <= std::numeric_limits<uint8_t>::max()) {
            byte(static_cast<uint8_t>((major << 5) | 24));
            byte(static_cast<uint8_t>(value));
        } else if (value <= std::numeric_limits<uint16_t>::max()) {
            byte(static_cast<uint8_t>((major << 5) | 25));
            byte(static_cast<uint8_t>((value >> 8) & 0xFF));
            byte(static_cast<uint8_t>(value & 0xFF));
        } else if (value <= std::numeric_limits<uint32_t>::max()) {
            byte(static_cast<uint8_t>((major << 5) | 26));
            for (int shift = 24; shift >= 0; shift -= 8) {
                byte(static_cast<uint8_t>((value >> shift) & 0xFF));
            }
        } else {
            byte(static_cast<uint8_t>((major << 5) | 27));
            for (int shift = 56; shift >= 0; shift -= 8) {
                byte(static_cast<uint8_t>((value >> shift) & 0xFF));
            }
        }
    }
    void byte(uint8_t value) {
        if (pos_ >= capacity_) {
            ok_ = false;
            return;
        }
        data_[pos_++] = value;
    }
    void bytes(const uint8_t* values, std::size_t len) {
        if (pos_ + len > capacity_) {
            ok_ = false;
            return;
        }
        std::memcpy(data_ + pos_, values, len);
        pos_ += len;
    }

    uint8_t* data_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

void setCborHeaders(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/cbor");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

esp_err_t WebServer::udpTelemetryGetHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendUdpTelemetry(req);
}

esp_err_t WebServer::udpTelemetryPostHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->updateUdpTelemetry(req);
}

esp_err_t WebServer::sendStateCbor(httpd_req_t* req) const {
    const domain::WeightState state = latest_.get();

    uint8_t buffer[kCborBufferSize]{};
    CborWriter cbor(buffer, sizeof(buffer));

    cbor.map(12);
    cbor.key("sequence"); cbor.u64(state.sample.sequence);
    cbor.key("timestampUs"); cbor.i64(state.sample.timestamp_us);
    cbor.key("valid"); cbor.boolean(state.sample.valid);
    cbor.key("cleanValid"); cbor.boolean(state.sample.clean_valid);
    cbor.key("rejectReason"); cbor.text(state.sample.reject_reason);
    cbor.key("rawSum"); cbor.i64(state.sample.raw_sum);
    cbor.key("cleanSum"); cbor.i64(state.sample.clean_sum);
    cbor.key("total"); cbor.f64(state.sample.total);
    cbor.key("weight"); cbor.f64(state.sample.weight);
    cbor.key("diagnosticPartialRead"); cbor.boolean(config::kHx711ReadReadySubsetForDiagnostics);

    cbor.key("target");
    cbor.map(4);
    cbor.key("stage"); cbor.text(config::kDefaultBatchStageName);
    cbor.key("weight"); cbor.f64(config::kDefaultBatchTargetWeight);
    cbor.key("remaining"); cbor.f64(config::kDefaultBatchTargetWeight - state.sample.weight);
    cbor.key("remainingShovels");
    cbor.f64(config::kDefaultShovelWeight > 0.0f
                 ? (config::kDefaultBatchTargetWeight - state.sample.weight) / config::kDefaultShovelWeight
                 : 0.0f);

    cbor.key("ma");
    cbor.array(state.filter_count);
    for (std::size_t i = 0; i < state.filter_count; ++i) {
        cbor.map(5);
        cbor.key("name"); cbor.text(state.filters[i].name);
        cbor.key("valid"); cbor.boolean(state.filters[i].valid);
        cbor.key("rawSum"); cbor.i64(state.filters[i].raw_sum);
        cbor.key("total"); cbor.f64(state.filters[i].total);
        cbor.key("weight"); cbor.f64(state.filters[i].weight);
    }

    if (!cbor.ok()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "cbor buffer too small");
    }

    setCborHeaders(req);
    return httpd_resp_send(req, reinterpret_cast<const char*>(cbor.data()), cbor.size());
}

esp_err_t WebServer::sendSettings(httpd_req_t* req) const {
    const domain::CalibrationState calibration = settings_.calibration();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "sumOffset", static_cast<double>(calibration.sum_offset));
    cJSON_AddNumberToObject(root, "sumScale", calibration.sum_scale);
    cJSON* units = cJSON_AddArrayToObject(root, "units");
    for (std::size_t i = 0; i < calibration.unit_count && i < domain::kMaxUnitConversions; ++i) {
        const domain::UnitConversion& unit = calibration.units[i];
        if (!unit.enabled) {
            continue;
        }

        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", unit.name);
        cJSON_AddNumberToObject(item, "rawPerUnit", unit.raw_per_unit);
        cJSON_AddItemToArray(units, item);
    }

    cJSON* setpoints = cJSON_AddArrayToObject(root, "setpoints");
    for (std::size_t i = 0; i < calibration.setpoint_count && i < domain::kMaxSetpoints; ++i) {
        const domain::Setpoint& setpoint = calibration.setpoints[i];
        if (!setpoint.enabled) {
            continue;
        }

        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", setpoint.name);
        cJSON_AddNumberToObject(item, "rawValue", static_cast<double>(setpoint.raw_value));
        cJSON_AddItemToArray(setpoints, item);
    }

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
    }

    setJsonHeaders(req);
    const esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

esp_err_t WebServer::updateSettings(httpd_req_t* req) {
    BodyBuffer body(nullptr, &std::free);
    esp_err_t body_err = readJsonBody(req, body);
    if (body_err != ESP_OK) {
        return body_err;
    }

    cJSON* root = cJSON_Parse(body.get());
    if (root == nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    domain::CalibrationState calibration = settings_.calibration();

    cJSON* sum_offset = cJSON_GetObjectItemCaseSensitive(root, "sumOffset");
    if (cJSON_IsNumber(sum_offset)) {
        calibration.sum_offset = static_cast<int64_t>(sum_offset->valuedouble);
    }

    cJSON* sum_scale = cJSON_GetObjectItemCaseSensitive(root, "sumScale");
    if (cJSON_IsNumber(sum_scale)) {
        calibration.sum_scale = static_cast<float>(sum_scale->valuedouble);
    }

    cJSON* units = cJSON_GetObjectItemCaseSensitive(root, "units");
    if (cJSON_IsArray(units)) {
        calibration.units = {};
        calibration.unit_count = 0;

        const int unit_count = cJSON_GetArraySize(units);
        for (int i = 0; i < unit_count && calibration.unit_count < domain::kMaxUnitConversions; ++i) {
            cJSON* item = cJSON_GetArrayItem(units, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
            cJSON* raw_per_unit = cJSON_GetObjectItemCaseSensitive(item, "rawPerUnit");
            if (!cJSON_IsString(name) ||
                !isEnglishLettersOnly(name->valuestring) ||
                !cJSON_IsNumber(raw_per_unit) ||
                raw_per_unit->valuedouble <= 0.0) {
                continue;
            }

            domain::UnitConversion& unit = calibration.units[calibration.unit_count++];
            unit.enabled = true;
            std::strncpy(unit.name, name->valuestring, domain::kUnitNameMaxLength);
            unit.name[domain::kUnitNameMaxLength] = '\0';
            unit.raw_per_unit = static_cast<float>(raw_per_unit->valuedouble);
        }
    }

    cJSON* setpoints = cJSON_GetObjectItemCaseSensitive(root, "setpoints");
    if (cJSON_IsArray(setpoints)) {
        calibration.setpoints = {};
        calibration.setpoint_count = 0;

        const int setpoint_count = cJSON_GetArraySize(setpoints);
        for (int i = 0; i < setpoint_count && calibration.setpoint_count < domain::kMaxSetpoints; ++i) {
            cJSON* item = cJSON_GetArrayItem(setpoints, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
            cJSON* raw_value = cJSON_GetObjectItemCaseSensitive(item, "rawValue");
            if (!cJSON_IsString(name) || name->valuestring == nullptr ||
                name->valuestring[0] == '\0' ||
                !cJSON_IsNumber(raw_value)) {
                continue;
            }

            domain::Setpoint& setpoint = calibration.setpoints[calibration.setpoint_count++];
            setpoint.enabled = true;
            std::strncpy(setpoint.name, name->valuestring, domain::kSetpointNameMaxLength);
            setpoint.name[domain::kSetpointNameMaxLength] = '\0';
            setpoint.raw_value = static_cast<int64_t>(raw_value->valuedouble);
        }
    }
    cJSON_Delete(root);

    const esp_err_t err = settings_.save(calibration);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "settings save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "settings save failed");
    }

    return sendSettings(req);
}

esp_err_t WebServer::sendWifi(httpd_req_t* req) const {
    const WifiStatus status = wifi_.status();
    const settings::WifiCredentials credentials = settings_.wifiCredentials();

    cJSON* root = cJSON_CreateObject();
    cJSON* ap = cJSON_AddObjectToObject(root, "ap");
    cJSON_AddBoolToObject(ap, "started", status.ap_started);
    cJSON_AddStringToObject(ap, "ssid", status.ap_ssid);
    cJSON_AddStringToObject(ap, "mac", status.ap_mac);

    cJSON* sta = cJSON_AddObjectToObject(root, "sta");
    cJSON_AddBoolToObject(sta, "configured", credentials.configured);
    cJSON_AddBoolToObject(sta, "connected", status.sta_connected);
    cJSON_AddStringToObject(sta, "ssid", status.sta_ssid[0] != '\0' ? status.sta_ssid : (credentials.networks[0].ssid[0] != '\0' ? credentials.networks[0].ssid : ""));
    cJSON_AddStringToObject(sta, "ip", status.sta_ip);
    cJSON_AddStringToObject(sta, "mac", status.sta_mac);

    bool active_has_password = false;
    for (std::size_t i = 0; i < settings::kMaxWifiNetworks; ++i) {
        if (std::strcmp(credentials.networks[i].ssid, status.sta_ssid) == 0) {
            active_has_password = (credentials.networks[i].password[0] != '\0');
            break;
        }
    }
    cJSON_AddBoolToObject(sta, "hasPassword", active_has_password);

    // Дополнительно возвращаем массив всех сохраненных сетей для Web UI
    cJSON* networks_arr = cJSON_AddArrayToObject(root, "networks");
    for (std::size_t i = 0; i < settings::kMaxWifiNetworks; ++i) {
        const auto& net = credentials.networks[i];
        if (net.ssid[0] != '\0') {
            cJSON* net_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(net_obj, "ssid", net.ssid);
            cJSON_AddBoolToObject(net_obj, "hasPassword", net.password[0] != '\0');
            cJSON_AddItemToArray(networks_arr, net_obj);
        }
    }

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
    }

    setJsonHeaders(req);
    const esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

esp_err_t WebServer::updateWifi(httpd_req_t* req) {
    BodyBuffer body(nullptr, &std::free);
    esp_err_t body_err = readJsonBody(req, body);
    if (body_err != ESP_OK) {
        return body_err;
    }

    cJSON* root = cJSON_Parse(body.get());
    if (root == nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON* password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring == nullptr) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid must be a string");
    }
    if (password != nullptr && !cJSON_IsString(password)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "password must be a string");
    }

    const char* ssid_str = ssid->valuestring;
    const char* password_str = (password != nullptr && password->valuestring != nullptr) ? password->valuestring : "";

    esp_err_t err = settings_.saveWifi(ssid_str, password_str);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "wifi save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi save failed");
    }

    err = wifi_.connect(settings_.wifiCredentials());
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "wifi reconnect failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi reconnect failed");
    }

    return sendWifi(req);
}

esp_err_t WebServer::sendWifiScan(httpd_req_t* req) const {
    auto networks = wifi_.scanNetworks();

    cJSON* root = cJSON_CreateArray();
    for (const auto& net : networks) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", net.ssid);
        cJSON_AddNumberToObject(item, "rssi", static_cast<double>(net.rssi));
        cJSON_AddBoolToObject(item, "secure", net.authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(root, item);
    }

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
    }

    setJsonHeaders(req);
    const esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

esp_err_t WebServer::sendUdpTelemetry(httpd_req_t* req) const {
    const settings::UdpTelemetrySettings settings = settings_.udpTelemetry();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", settings.enabled);
    cJSON_AddNumberToObject(root, "scaleId", static_cast<double>(settings.scale_id));
    cJSON_AddStringToObject(root, "targetHost", settings.target_host);
    cJSON_AddNumberToObject(root, "port", settings.port);

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json allocation failed");
    }

    setJsonHeaders(req);
    const esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

esp_err_t WebServer::updateUdpTelemetry(httpd_req_t* req) {
    BodyBuffer body(nullptr, &std::free);
    esp_err_t body_err = readJsonBody(req, body);
    if (body_err != ESP_OK) {
        return body_err;
    }

    cJSON* root = cJSON_Parse(body.get());
    if (root == nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    settings::UdpTelemetrySettings udp = settings_.udpTelemetry();
    cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (cJSON_IsBool(enabled)) {
        udp.enabled = cJSON_IsTrue(enabled);
    }

    cJSON* scale_id = cJSON_GetObjectItemCaseSensitive(root, "scaleId");
    if (cJSON_IsNumber(scale_id) && scale_id->valueint > 0) {
        udp.scale_id = static_cast<uint32_t>(scale_id->valueint);
    }

    cJSON* target_host = cJSON_GetObjectItemCaseSensitive(root, "targetHost");
    if (cJSON_IsString(target_host) && target_host->valuestring != nullptr) {
        std::strncpy(udp.target_host, target_host->valuestring, sizeof(udp.target_host) - 1);
        udp.target_host[sizeof(udp.target_host) - 1] = '\0';
    }

    cJSON* port = cJSON_GetObjectItemCaseSensitive(root, "port");
    if (cJSON_IsNumber(port) && port->valueint > 0 && port->valueint <= 65535) {
        udp.port = static_cast<uint16_t>(port->valueint);
    }
    cJSON_Delete(root);

    const esp_err_t err = settings_.saveUdpTelemetry(udp);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "UDP telemetry save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "UDP telemetry save failed");
    }

    return sendUdpTelemetry(req);
}

esp_err_t WebServer::sendStaticFile(httpd_req_t* req) const {
    char path[160]{};
    const char* uri = req->uri;
    
    if (std::strncmp(uri, "http://", 7) == 0) {
        uri = std::strchr(uri + 7, '/');
    } else if (std::strncmp(uri, "https://", 8) == 0) {
        uri = std::strchr(uri + 8, '/');
    }
    
    if (uri == nullptr || std::strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    char clean_uri[128]{};
    std::strncpy(clean_uri, uri, sizeof(clean_uri) - 1);
    char* query_start = std::strchr(clean_uri, '?');
    if (query_start != nullptr) {
        *query_start = '\0';
    }

    if (std::strstr(clean_uri, "..") != nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid path");
    }

    const int written = std::snprintf(path, sizeof(path), "%s%s", assets_.basePath(), clean_uri);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "path too long");
    }

    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
    }

    httpd_resp_set_type(req, contentTypeForPath(path));

    char buffer[kFileBufferSize]{};
    while (true) {
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file);
        if (read > 0) {
            const esp_err_t err = httpd_resp_send_chunk(req, buffer, read);
            if (err != ESP_OK) {
                std::fclose(file);
                return err;
            }
        }

        if (read < sizeof(buffer)) {
            break;
        }
    }

    std::fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

}  // пространство имен mixer::web
