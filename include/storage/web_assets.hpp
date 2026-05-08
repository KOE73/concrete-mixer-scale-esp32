#pragma once

#include "esp_err.h"

namespace mixer::storage {

// Монтирует SPIFFS-раздел со статическими Web-файлами. Он отделен от NVS:
// настройки и калибровка остаются в NVS, а HTML/CSS/JS живут в разделе www.
class WebAssets {
public:
    esp_err_t mount();
    const char* basePath() const;

private:
    bool mounted_ = false;
};

}  // пространство имен mixer::storage
