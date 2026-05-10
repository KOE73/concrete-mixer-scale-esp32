#include "display/spinner.hpp"

#include "esp_timer.h"

#include <algorithm>
#include <cmath>

namespace mixer::display {

namespace {
    constexpr float kPi = 3.14159265358979323846f;

    // Вспомогательная функция для рисования линии (алгоритм Брезенхема)
    // Решение использовать свою реализацию: мы не зависим от Adafruit GFX или HUB75,
    // что позволяет классу Spinner быть абсолютно универсальным и автономным.
    void drawLine(int x0, int y0, int x1, int y1, const Spinner::Color& color, const Spinner::DrawPixelFunc& draw_pixel) {
        int dx = std::abs(x1 - x0);
        int dy = -std::abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            draw_pixel(x0, y0, color.r, color.g, color.b);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
} 

Spinner::Spinner(int x, int y, int width, int height)
    : x_(x), y_(y), width_(width), height_(height)
{
    // Центр и радиусы с учетом того, что координаты пикселей дискретны.
    // Если ширина 9, то пиксели: x .. x+8. Радиус будет 4. Центр x+4.
    float w_span = std::max(0, width_ - 1);
    float h_span = std::max(0, height_ - 1);

    center_x_ = x_ + w_span / 2.0f;
    center_y_ = y_ + h_span / 2.0f;
    radius_x_ = w_span / 2.0f;
    radius_y_ = h_span / 2.0f;

    // Расчет оптимального шага в градусах:
    // Длина окружности (приближенно по максимуму из измерений) = Pi * max(W, H)
    // Чтобы на дуге не было "дырок", нужно делать шаг так, чтобы смещение было не более 1 пикселя.
    // Таким образом мы адаптируемся как под размер 9x9, так и под крупные.
    float max_dim = std::max(width_, height_);
    float circumference = kPi * max_dim;
    if (circumference > 0) {
        step_deg_ = 360.0f / circumference;
    } else {
        step_deg_ = 10.0f; // Безопасное значение по умолчанию
    }
}

void Spinner::setSpeedRpm(float rpm) {
    rpm_ = rpm;
}

void Spinner::setTrailLength(float length_deg) {
    trail_length_deg_ = length_deg;
}

void Spinner::setRayColor(Color c) {
    ray_color_ = c;
}

void Spinner::setTrailColor(Color c) {
    style_ = Style::Custom;
    custom_trail_color_ = c;
}

void Spinner::setRadarStyle() {
    style_ = Style::Radar;
}

void Spinner::setFireStyle() {
    style_ = Style::Fire;
}

Spinner::Color Spinner::calculateTrailColor(float offset_deg) const {
    // offset_deg = 0 (это сам луч/голова), offset_deg = trail_length_deg_ (это конец шлейфа)
    float intensity = 1.0f - (offset_deg / trail_length_deg_);
    if (intensity < 0.0f) intensity = 0.0f;

    // Голова луча (первые 1-2 шага расчета) рисуется заданным цветом (белым по умолчанию)
    if (offset_deg < step_deg_ * 1.5f) {
        return ray_color_;
    }

    if (style_ == Style::Radar) {
        // Зеленый как на радаре: затухающий до нуля
        return { 0, static_cast<uint8_t>(255 * intensity), 0 };
    } 
    else if (style_ == Style::Fire) {
        // Желто-красный огонь. Голова: желто-белая, середина: красная, хвост: темный.
        // intensity: 1.0 -> 0.0
        uint8_t r = static_cast<uint8_t>(255 * std::sqrt(intensity)); // Красный держится дольше
        uint8_t g = static_cast<uint8_t>(255 * intensity * intensity); // Зеленый спадает быстрее (дает желтый)
        return { r, g, 0 };
    }
    else {
        // Пользовательский цвет затухания
        return {
            static_cast<uint8_t>(custom_trail_color_.r * intensity),
            static_cast<uint8_t>(custom_trail_color_.g * intensity),
            static_cast<uint8_t>(custom_trail_color_.b * intensity)
        };
    }
}

void Spinner::draw(const DrawPixelFunc& draw_pixel) {
    // Очищаем область спиннера фоновым черным цветом
    for (int cy = y_; cy < y_ + height_; ++cy) {
        for (int cx = x_; cx < x_ + width_; ++cx) {
            draw_pixel(cx, cy, 0, 0, 0);
        }
    }

    // Вычисляем текущий угол на основе системного времени (полная независимость от FPS)
    int64_t now_us = esp_timer_get_time();
    double seconds = static_cast<double>(now_us) / 1000000.0;
    
    // rpm_ - оборотов в минуту. rpm / 60 - оборотов в секунду.
    double rps = rpm_ / 60.0;
    
    // Текущий угол в градусах: время * скорость (об/сек) * 360
    double current_angle = std::fmod(seconds * rps * 360.0, 360.0);
    
    // Рисуем шлейф от хвоста к голове, чтобы наложения пикселей 
    // поверх отрисовывались более яркими цветами головы.
    for (float offset = trail_length_deg_; offset >= 0.0f; offset -= step_deg_) {
        float angle_deg = current_angle - offset;
        if (angle_deg < 0.0f) {
            angle_deg += 360.0f;
        }

        float angle_rad = angle_deg * kPi / 180.0f;
        
        // Координаты конца отрезка.
        // Вычитаем 90 градусов для старта сверху (за счет перестановки sin/cos).
        // x = center_x + radius_x * sin(angle)
        // y = center_y - radius_y * cos(angle)
        int x1 = static_cast<int>(std::round(center_x_ + radius_x_ * std::sin(angle_rad)));
        int y1 = static_cast<int>(std::round(center_y_ - radius_y_ * std::cos(angle_rad)));
        
        Color c = calculateTrailColor(offset);
        
        drawLine(static_cast<int>(center_x_), static_cast<int>(center_y_), x1, y1, c, draw_pixel);
    }
}

} // namespace mixer::display
