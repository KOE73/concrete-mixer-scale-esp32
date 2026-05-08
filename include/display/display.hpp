#pragma once

#include "processing/weight_processor.hpp"

#include "esp_err.h"

#include <memory>

namespace mixer::display {

// Представление состояния веса для вывода. Здесь только то, что нужно устройству
// индикации, поэтому HUB75, LCD и лог могут использовать один контракт.
struct DisplayFrame {
    const char* stage_name = "";
    float weight = 0.0f;
    float target_weight = 0.0f;
    float remaining_weight = 0.0f;
    float remaining_shovels = 0.0f;
    bool valid = false;
};

// Граница для конкретных устройств вывода. Первая реализация пишет в лог;
// будущий HUB75 должен подключаться здесь и не лезть в модуль измерения.
class IDisplaySink {
public:
    virtual ~IDisplaySink() = default;

    virtual esp_err_t begin() = 0;
    virtual void render(const DisplayFrame& frame) = 0;
};

// Минимальная реализация индикации для первичного запуска без матрицы. Она
// проверяет контракт задачи индикации и не добавляет зависимость от железа.
class LogDisplaySink final : public IDisplaySink {
public:
    esp_err_t begin() override;
    void render(const DisplayFrame& frame) override;
};

// Реализация вывода на HUB75 64x64 через ESP32-HUB75-MatrixPanel-DMA. Она
// получает уже подготовленный DisplayFrame и не знает, как читаются датчики или
// где хранится калибровка.
class Hub75DisplaySink final : public IDisplaySink {
public:
    Hub75DisplaySink();
    ~Hub75DisplaySink() override;

    esp_err_t begin() override;
    void render(const DisplayFrame& frame) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Периодический рендерер, который превращает LatestWeightStore в DisplayFrame.
// Он отделен от Web, чтобы визуальная индикация развивалась независимо.
class DisplayTask {
public:
    DisplayTask(processing::LatestWeightStore& latest, IDisplaySink& sink);

    esp_err_t start();

private:
    static void taskEntry(void* context);
    void run();

    processing::LatestWeightStore& latest_;
    IDisplaySink& sink_;
};

}  // пространство имен mixer::display
