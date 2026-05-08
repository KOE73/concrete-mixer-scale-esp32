#pragma once

#include <cstdint>

#if __has_include("config/wifi_secrets.hpp")
#include "config/wifi_secrets.hpp"
#else
#include "config/wifi_secrets.example.hpp"
#endif

namespace mixer::config {

// Режим сети сделан как AP+STA: своя точка доступа всегда остается включенной,
// а STA-интерфейс дополнительно подключается к роутеру, если есть сохраненные
// или прошитые учетные данные. Это важно для весов: даже при ошибке пароля,
// смене роутера или отсутствии цеховой сети к устройству все равно можно
// подключиться напрямую через AP и исправить настройки в Web UI.
inline constexpr char kApSsid[] = "MixerScale";
inline constexpr char kApPassword[] = "mixerscale";
inline constexpr uint8_t kApChannel = 6;
inline constexpr uint8_t kApMaxConnections = 4;

// Файл include/config/wifi_secrets.hpp намеренно добавлен в .gitignore.
// Для локальной прошивки впиши туда SSID/пароль роутера, если хочешь, чтобы
// устройство подключилось к сети сразу после чистой прошивки:
//
//   #pragma once
//   namespace mixer::config::secrets {
//   inline constexpr char kStaSsid[] = "WorkshopWiFi";
//   inline constexpr char kStaPassword[] = "secret-password";
//   }
//
// Если файл пустой или отсутствует, STA не стартует до тех пор, пока SSID и
// пароль не будут введены через Web UI. После сохранения через UI данные лежат
// в NVS и имеют приоритет над прошитым секретом.
inline constexpr const char* kDefaultStaSsid = secrets::kStaSsid;
inline constexpr const char* kDefaultStaPassword = secrets::kStaPassword;

}  // пространство имен mixer::config
