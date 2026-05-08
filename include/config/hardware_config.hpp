#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "hx711.h"

namespace mixer::config {

enum class DisplayDriver {
    Hub75,
    Log,
};

// Выбирает способ чтения HX711. Последовательный драйвер оставляет проверенное
// поведение библиотеки, а общая шина SCK нужна для схемы, где все АЦП
// тактируются вместе и разброс по времени между каналами меньше.
enum class Hx711ReadDriver {
    EspIdfLibSequential,
    SharedClockBus,
};

// Описывает один физический канал HX711. Калибровка лежит здесь, потому что
// offset и scale относятся к конкретной установке датчика, а обработка дальше
// работает уже с нормализованными массивами из этой конфигурации.
struct Hx711ChannelConfig {
    const char* name;
    gpio_num_t dout_pin;
    gpio_num_t sck_pin;
    hx711_gain_t gain;
    int32_t offset;
    float scale;
    bool enabled;
};

// Чтобы добавить или убрать тензодатчики, меняй этот массив. Остальная прошивка
// использует kLoadCellCount и не должна прошивать в логике ровно 3 канала.
inline constexpr std::array<Hx711ChannelConfig, 3> kLoadCells{{
    {"front_left", GPIO_NUM_4, GPIO_NUM_5, HX711_GAIN_A_128, 0, 1.0f, true},
    {"front_right", GPIO_NUM_6, GPIO_NUM_7, HX711_GAIN_A_128, 0, 1.0f, true},
    {"rear", GPIO_NUM_8, GPIO_NUM_9, HX711_GAIN_A_128, 0, 1.0f, true},
}};

inline constexpr Hx711ReadDriver kHx711ReadDriver = Hx711ReadDriver::EspIdfLibSequential;
inline constexpr std::size_t kLoadCellCount = kLoadCells.size();

inline constexpr float kDefaultGlobalScale = 1.0f;
inline constexpr char kDefaultBatchStageName[] = "material";
inline constexpr float kDefaultBatchTargetWeight = 100.0f;
inline constexpr float kDefaultShovelWeight = 5.0f;

inline constexpr uint32_t kSamplePeriodMs = 200;
inline constexpr uint32_t kHx711ReadyTimeoutMs = 120;
inline constexpr uint32_t kProcessorTaskStackBytes = 4096;
inline constexpr uint32_t kSamplerTaskStackBytes = 4096;
inline constexpr uint32_t kDisplayTaskStackBytes = 4096;
inline constexpr uint32_t kWebServerTaskStackBytes = 8192;

inline constexpr uint32_t kDisplayRefreshPeriodMs = 500;
inline constexpr uint32_t kMovingAverageWindow = 8;
inline constexpr float kExponentialAlpha = 0.25f;

inline constexpr DisplayDriver kDisplayDriver = DisplayDriver::Hub75;
inline constexpr int kHub75Width = 64;
inline constexpr int kHub75Height = 64;
inline constexpr int kHub75ChainLength = 1;
inline constexpr uint8_t kHub75Brightness = 96;

// Пины Matrix Portal S3 для HUB75 из документации ESP32-HUB75-MatrixPanel-DMA,
// которая ссылается на Adafruit Protomatter mapping для этой платы.
inline constexpr int kHub75R1Pin = 42;
inline constexpr int kHub75G1Pin = 41;
inline constexpr int kHub75B1Pin = 40;
inline constexpr int kHub75R2Pin = 38;
inline constexpr int kHub75G2Pin = 39;
inline constexpr int kHub75B2Pin = 37;
inline constexpr int kHub75APin = 45;
inline constexpr int kHub75BPin = 36;
inline constexpr int kHub75CPin = 48;
inline constexpr int kHub75DPin = 35;
inline constexpr int kHub75EPin = 21;
inline constexpr int kHub75LatPin = 47;
inline constexpr int kHub75OePin = 14;
inline constexpr int kHub75ClkPin = 2;

}  // пространство имен mixer::config
