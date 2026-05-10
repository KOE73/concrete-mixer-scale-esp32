#include "web/web_server.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "config/hardware_config.hpp"
#include "esp_log.h"

namespace mixer::web {
namespace {

constexpr char kTag[] = "web";
constexpr std::size_t kPostBufferSize = 2048;
constexpr std::size_t kFileBufferSize = 1024;

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

}  // анонимное пространство имен

WebServer::WebServer(processing::LatestWeightStore& latest,
                     settings::SettingsStore& settings,
                     storage::WebAssets& assets,
                     WifiManager& wifi)
    : latest_(latest), settings_(settings), assets_(assets), wifi_(wifi) {}

esp_err_t WebServer::start() {
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.stack_size = config::kWebServerTaskStackBytes;
    http_config.lru_purge_enable = true;
    http_config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server_, &http_config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t weight{};
    weight.uri = "/api/weight";
    weight.method = HTTP_GET;
    weight.handler = &WebServer::weightHandler;
    weight.user_ctx = this;
    httpd_register_uri_handler(server_, &weight);

    httpd_uri_t settings_get{};
    settings_get.uri = "/api/settings";
    settings_get.method = HTTP_GET;
    settings_get.handler = &WebServer::settingsGetHandler;
    settings_get.user_ctx = this;
    httpd_register_uri_handler(server_, &settings_get);

    httpd_uri_t settings_post{};
    settings_post.uri = "/api/settings";
    settings_post.method = HTTP_POST;
    settings_post.handler = &WebServer::settingsPostHandler;
    settings_post.user_ctx = this;
    httpd_register_uri_handler(server_, &settings_post);

    httpd_uri_t wifi_get{};
    wifi_get.uri = "/api/wifi";
    wifi_get.method = HTTP_GET;
    wifi_get.handler = &WebServer::wifiGetHandler;
    wifi_get.user_ctx = this;
    httpd_register_uri_handler(server_, &wifi_get);

    httpd_uri_t wifi_post{};
    wifi_post.uri = "/api/wifi";
    wifi_post.method = HTTP_POST;
    wifi_post.handler = &WebServer::wifiPostHandler;
    wifi_post.user_ctx = this;
    httpd_register_uri_handler(server_, &wifi_post);

    httpd_uri_t static_files{};
    static_files.uri = "/*";
    static_files.method = HTTP_GET;
    static_files.handler = &WebServer::staticFileHandler;
    static_files.user_ctx = this;
    httpd_register_uri_handler(server_, &static_files);

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

esp_err_t WebServer::weightHandler(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->sendWeight(req);
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

esp_err_t WebServer::sendWeight(httpd_req_t* req) const {
    const domain::WeightState state = latest_.get();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "sequence", static_cast<double>(state.sample.sequence));
    cJSON_AddNumberToObject(root, "timestampUs", static_cast<double>(state.sample.timestamp_us));
    cJSON_AddBoolToObject(root, "valid", state.sample.valid);
    cJSON_AddNumberToObject(root, "total", state.sample.total);
    cJSON_AddNumberToObject(root, "weight", state.sample.weight);
    cJSON_AddBoolToObject(root, "diagnosticPartialRead",
                          config::kHx711ReadReadySubsetForDiagnostics);

    cJSON* target = cJSON_AddObjectToObject(root, "target");
    cJSON_AddStringToObject(target, "stage", config::kDefaultBatchStageName);
    cJSON_AddNumberToObject(target, "weight", config::kDefaultBatchTargetWeight);
    cJSON_AddNumberToObject(target, "remaining",
                            config::kDefaultBatchTargetWeight - state.sample.weight);
    cJSON_AddNumberToObject(
        target,
        "remainingShovels",
        config::kDefaultShovelWeight > 0.0f
            ? (config::kDefaultBatchTargetWeight - state.sample.weight) /
                  config::kDefaultShovelWeight
            : 0.0f);

    cJSON* channels = cJSON_AddArrayToObject(root, "channels");
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        cJSON* channel = cJSON_CreateObject();
        cJSON_AddNumberToObject(channel, "index", static_cast<double>(i));
        cJSON_AddStringToObject(channel, "name", config::kLoadCells[i].name);
        cJSON_AddBoolToObject(channel, "active", config::kLoadCells[i].enabled);
        cJSON_AddBoolToObject(channel, "ready", state.sample.ready[i]);
        cJSON_AddNumberToObject(channel, "raw", state.sample.raw[i]);
        cJSON_AddNumberToObject(channel, "weight", state.sample.channels[i]);
        cJSON_AddItemToArray(channels, channel);
    }

    cJSON* filters = cJSON_AddArrayToObject(root, "filters");
    for (std::size_t i = 0; i < state.filter_count; ++i) {
        cJSON* filter = cJSON_CreateObject();
        cJSON_AddStringToObject(filter, "name", state.filters[i].name);
        cJSON_AddBoolToObject(filter, "valid", state.filters[i].valid);
        cJSON_AddNumberToObject(filter, "total", state.filters[i].total);
        cJSON_AddNumberToObject(filter, "weight", state.filters[i].weight);
        cJSON_AddItemToArray(filters, filter);
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

esp_err_t WebServer::sendSettings(httpd_req_t* req) const {
    const domain::CalibrationState calibration = settings_.calibration();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "globalScale", calibration.global_scale);
    cJSON* channels = cJSON_AddArrayToObject(root, "channels");
    for (std::size_t i = 0; i < config::kLoadCellCount; ++i) {
        cJSON* channel = cJSON_CreateObject();
        cJSON_AddNumberToObject(channel, "index", static_cast<double>(i));
        cJSON_AddStringToObject(channel, "name", config::kLoadCells[i].name);
        cJSON_AddNumberToObject(channel, "offset", calibration.offsets[i]);
        cJSON_AddNumberToObject(channel, "scale", calibration.scales[i]);
        cJSON_AddItemToArray(channels, channel);
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
    if (req->content_len >= kPostBufferSize) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    }

    char buffer[kPostBufferSize]{};
    std::size_t received = 0;
    while (received < req->content_len) {
        const int read = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (read <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
        }
        received += static_cast<std::size_t>(read);
    }

    cJSON* root = cJSON_Parse(buffer);
    if (root == nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    domain::CalibrationState calibration = settings_.calibration();

    cJSON* global_scale = cJSON_GetObjectItemCaseSensitive(root, "globalScale");
    if (cJSON_IsNumber(global_scale)) {
        calibration.global_scale = static_cast<float>(global_scale->valuedouble);
    }

    cJSON* channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (channels != nullptr && !cJSON_IsArray(channels)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "channels must be an array");
    }

    cJSON* channel = nullptr;
    cJSON_ArrayForEach(channel, channels) {
        cJSON* index_value = cJSON_GetObjectItemCaseSensitive(channel, "index");
        if (!cJSON_IsNumber(index_value)) {
            continue;
        }

        const int index = index_value->valueint;
        if (index < 0 || static_cast<std::size_t>(index) >= config::kLoadCellCount) {
            continue;
        }

        cJSON* offset = cJSON_GetObjectItemCaseSensitive(channel, "offset");
        if (cJSON_IsNumber(offset)) {
            calibration.offsets[static_cast<std::size_t>(index)] = offset->valueint;
        }

        cJSON* scale = cJSON_GetObjectItemCaseSensitive(channel, "scale");
        if (cJSON_IsNumber(scale)) {
            calibration.scales[static_cast<std::size_t>(index)] =
                static_cast<float>(scale->valuedouble);
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

    cJSON* sta = cJSON_AddObjectToObject(root, "sta");
    cJSON_AddBoolToObject(sta, "configured", credentials.configured);
    cJSON_AddBoolToObject(sta, "connected", status.sta_connected);
    cJSON_AddStringToObject(sta, "ssid", credentials.ssid);
    cJSON_AddStringToObject(sta, "ip", status.sta_ip);
    cJSON_AddBoolToObject(sta, "hasPassword", credentials.password[0] != '\0');

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
    if (req->content_len >= kPostBufferSize) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    }

    char buffer[kPostBufferSize]{};
    std::size_t received = 0;
    while (received < req->content_len) {
        const int read = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (read <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
        }
        received += static_cast<std::size_t>(read);
    }

    cJSON* root = cJSON_Parse(buffer);
    if (root == nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    settings::WifiCredentials credentials{};
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

    std::strncpy(credentials.ssid, ssid->valuestring, sizeof(credentials.ssid) - 1);
    if (password != nullptr && password->valuestring != nullptr) {
        std::strncpy(credentials.password,
                     password->valuestring,
                     sizeof(credentials.password) - 1);
    }
    credentials.configured = credentials.ssid[0] != '\0';
    cJSON_Delete(root);

    esp_err_t err = settings_.saveWifi(credentials);
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

esp_err_t WebServer::sendStaticFile(httpd_req_t* req) const {
    char path[160]{};
    const char* uri = req->uri;
    if (std::strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    if (std::strstr(uri, "..") != nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid path");
    }

    const int written = std::snprintf(path, sizeof(path), "%s%s", assets_.basePath(), uri);
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
