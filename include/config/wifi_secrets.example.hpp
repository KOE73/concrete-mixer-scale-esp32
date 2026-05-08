#pragma once

namespace mixer::config::secrets {

// Пример локального секрета для подключения к существующей Wi-Fi сети.
// Скопируй этот файл в include/config/wifi_secrets.hpp и впиши реальные данные.
// wifi_secrets.hpp не попадает в Git, чтобы пароль не утек в репозиторий.
inline constexpr char kStaSsid[] = "";
inline constexpr char kStaPassword[] = "";

}  // пространство имен mixer::config::secrets
