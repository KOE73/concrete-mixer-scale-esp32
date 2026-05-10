#pragma once

#include <cstdint>
#include <functional>

namespace mixer::display {

/**
 * @brief Представление спиннера активности.
 * Решение выделить спиннер в отдельный класс принято для инкапсуляции 
 * математики вращения, настройки следа (шлейфа), скорости и цвета.
 * Это позволяет переиспользовать его с разными размерами, стилями и 
 * на разных типах экранов, не привязываясь к HUB75.
 */
class Spinner {
public:
    struct Color {
        uint8_t r, g, b;
    };

    /**
     * @brief Тип функции отрисовки пикселя.
     * Колбэк абстрагирует класс от конкретной библиотеки дисплея.
     */
    using DrawPixelFunc = std::function<void(int x, int y, uint8_t r, uint8_t g, uint8_t b)>;

    /**
     * @brief Конструктор спиннера.
     * @param x, y Координаты левого верхнего угла.
     * @param width, height Размеры области (ширина и высота).
     */
    Spinner(int x, int y, int width, int height);

    /**
     * @brief Настраивает период вращения (скорость).
     * @param rpm Обороты в минуту (Revolutions Per Minute).
     */
    void setSpeedRpm(float rpm);

    /**
     * @brief Настраивает длину шлейфа.
     * @param length_deg Длина в градусах (например, 90 или 180).
     */
    void setTrailLength(float length_deg);

    /**
     * @brief Устанавливает цвет основного луча (головы).
     */
    void setRayColor(Color c);

    /**
     * @brief Устанавливает простой затухающий пользовательский цвет шлейфа.
     */
    void setTrailColor(Color c);

    /**
     * @brief Устанавливает стиль шлейфа: Радар (Зеленый затухающий).
     */
    void setRadarStyle();

    /**
     * @brief Устанавливает стиль шлейфа: Огонь (Желто-красный).
     */
    void setFireStyle();

    /**
     * @brief Отрисовывает спиннер.
     * Рассчитывает текущий угол на основе системного времени (esp_timer_get_time) 
     * и рисует очистку области, а затем луч со шлейфом через переданный колбэк.
     * @param draw_pixel Функция рисования пикселя.
     */
    void draw(const DrawPixelFunc& draw_pixel);

private:
    int x_, y_;
    int width_, height_;
    float center_x_, center_y_;
    float radius_x_, radius_y_;

    float step_deg_; // Шаг расчета в градусах, вычисляемый от размеров спиннера.

    float rpm_ = 60.0f; // Оборотов в минуту по умолчанию
    float trail_length_deg_ = 120.0f; // Длина шлейфа в градусах

    enum class Style { Radar, Fire, Custom };
    Style style_ = Style::Radar;
    Color ray_color_ = {255, 255, 255}; // Белый луч по умолчанию
    Color custom_trail_color_ = {255, 255, 255};

    /**
     * @brief Внутренняя функция вычисления цвета в зависимости от угла отставания от луча.
     */
    Color calculateTrailColor(float offset_deg) const;
};

} // namespace mixer::display
