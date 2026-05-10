#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace mixer::display {

// Общая база для компактной линейной шкалы на малом дисплее.
// База намеренно не знает, как именно заливать шкалу: она хранит геометрию,
// рамку, диапазоны, уставки и переводит значения в пиксели. Конкретный вид
// закраски задается потомками через drawFill().
class LinearIndicatorBase {
public:
    // Цвет хранится в RGB888, потому что HUB75 sink напрямую рисует такими
    // компонентами, а сам виджет не зависит от конкретной библиотеки дисплея.
    struct Color {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    // Диапазон описывает смысловую зону шкалы: например недобор, допуск,
    // перебор. Потомок сам решает, рисовать зоны как сегменты или как слои.
    struct Range {
        float from;
        float to;
        Color color1;
        Color color2;
    };

    // Уставка - отдельная отметка внутри шкалы. Цвет вынесен сюда, чтобы
    // критичные и справочные отметки можно было различать даже на 64x64.
    struct Setpoint {
        float value;
        Color color;
    };

    // На HUB75 каждый пиксель дорогой: иногда важнее сохранить рамку, иногда
    // отдать ее площадь под полезную заливку.
    enum class FillBounds {
        InsideFrame,
        IncludeFrame,
    };

    enum class Direction {
        Horizontal,
        Vertical,
    };

    using DrawPixelFunc = std::function<void(int, int, Color)>;
    using FillRectFunc = std::function<void(int, int, int, int, Color)>;

    /**
     * @brief Создает индикатор в прямоугольной области экрана.
     * @param x Левая координата области индикатора.
     * @param y Верхняя координата области индикатора.
     * @param width Полная ширина индикатора вместе с рамкой.
     * @param height Полная высота индикатора вместе с рамкой.
     */
    LinearIndicatorBase(int x, int y, int width, int height);
    virtual ~LinearIndicatorBase() = default;

    /**
     * @brief Задает общий числовой диапазон шкалы.
     * @details Используется для перевода веса в пиксели и для позиционирования
     * уставок. Если maximum меньше или равен minimum, класс сам делает диапазон
     * минимально валидным, чтобы не было деления на ноль.
     */
    void setValueRange(float minimum, float maximum);

    /**
     * @brief Включает или отключает рамку индикатора.
     * @details Цвет рамки обычно делают темно-серым, чтобы она была видна на
     * HUB75, но не спорила с полезной заливкой.
     */
    void setFrame(bool enabled, Color color = {32, 32, 32});

    /**
     * @brief Выбирает, занимает ли заливка пиксели рамки.
     * @details InsideFrame оставляет рамку отдельной. IncludeFrame отдает всю
     * область под заливку, что полезно на маленькой матрице, где каждый пиксель
     * важен.
     */
    void setFillBounds(FillBounds bounds);

    /**
     * @brief Выбирает направление заполнения шкалы.
     * @details Horizontal растет слева направо. Vertical растет снизу вверх:
     * так вертикальная шкала ведет себя как обычный столбик уровня.
     */
    void setDirection(Direction direction);

    /**
     * @brief Удаляет все цветовые диапазоны.
     * @details Вызывай перед повторной настройкой шкалы, если набор диапазонов
     * должен быть заменен полностью.
     */
    void clearRanges();

    /**
     * @brief Добавляет диапазон с основным цветом и тем же цветом для неактивного состояния.
     * @details Подходит для режимов, где неактивный цвет не нужен. Для overlay
     * лучше использовать перегрузку с color2, чтобы предыдущие слои не сливались
     * с текущим активным диапазоном.
     */
    bool addRange(float from, float to, Color color);

    /**
     * @brief Добавляет диапазон с отдельными активным и неактивным цветами.
     * @details color1 используется для текущего активного заполнения диапазона.
     * color2 используется overlay-режимом, когда диапазон уже пройден и поверх
     * него начал рисоваться следующий слой.
     */
    bool addRange(float from, float to, Color color1, Color color2);

    /**
     * @brief Удаляет все уставки.
     * @details Уставки независимы от диапазонов и рисуются поверх заливки.
     */
    void clearSetpoints();

    /**
     * @brief Добавляет отметку уставки на шкалу.
     * @details value задается в тех же единицах, что и setValueRange(). color
     * выбирай контрастным к заливке, иначе отметка потеряется на HUB75.
     */
    bool addSetpoint(float value, Color color);

    /**
     * @brief Рисует индикатор через переданные функции пикселя и прямоугольника.
     * @details Класс не знает про конкретный дисплей. HUB75, LCD или тестовый
     * рендерер передают сюда свои функции отрисовки.
     */
    void draw(float value, const FillRectFunc& fill_rect, const DrawPixelFunc& draw_pixel) const;

protected:
    // Рабочая область заливки после учета рамки. Потомкам передается уже
    // готовая область, чтобы они не дублировали расчеты x/y/width/height.
    struct FillArea {
        int x;
        int y;
        int width;
        int height;
    };

    static constexpr std::size_t kMaxRanges = 4;
    static constexpr std::size_t kMaxSetpoints = 4;

    // Единственная обязательная точка расширения: потомок получает текущее
    // значение и рисует заполнение в уже вычисленной рабочей области.
    virtual void drawFill(float value,
                          FillArea area,
                          const FillRectFunc& fill_rect) const = 0;

    // По умолчанию уставки видны всегда. Overlay-вариант переопределяет это,
    // чтобы отметка появлялась только после входа значения в нужный диапазон.
    virtual bool shouldDrawSetpoint(const Setpoint& setpoint, float value) const;

    int valueToX(float value, FillArea area) const;
    int widthToValue(float value, FillArea area) const;
    float valueAtOffset(int offset, FillArea area) const;
    int valueToOffset(float value, FillArea area) const;
    int fillLength(FillArea area) const;
    bool isVertical() const;
    void fillFromStart(int filled,
                       FillArea area,
                       Color color,
                       const FillRectFunc& fill_rect) const;
    void fillAtOffset(int offset,
                      FillArea area,
                      Color color,
                      const FillRectFunc& fill_rect) const;
    float clampedValue(float value) const;
    const std::array<Range, kMaxRanges>& ranges() const;
    std::size_t rangeCount() const;
    Color colorForValue(float value) const;

private:
    FillArea fillArea() const;
    void drawFrame(const FillRectFunc& fill_rect) const;
    void drawSetpoints(float value, FillArea area, const DrawPixelFunc& draw_pixel) const;

    int x_;
    int y_;
    int width_;
    int height_;
    float minimum_ = 0.0f;
    float maximum_ = 100.0f;
    bool frame_enabled_ = true;
    Color frame_color_{32, 32, 32};
    FillBounds fill_bounds_ = FillBounds::InsideFrame;
    Direction direction_ = Direction::Horizontal;
    std::array<Range, kMaxRanges> ranges_{};
    std::size_t range_count_ = 0;
    std::array<Setpoint, kMaxSetpoints> setpoints_{};
    std::size_t setpoint_count_ = 0;
};

// Самый простой режим: одна линейная заливка одним цветом от минимума к максимуму.
class SolidLinearIndicator final : public LinearIndicatorBase {
public:
    using LinearIndicatorBase::LinearIndicatorBase;

    /**
     * @brief Задает цвет простой линейной заливки.
     * @details В этом режиме диапазоны не используются: вся заполненная часть
     * рисуется одним выбранным цветом.
     */
    void setColor(Color color);

private:
    void drawFill(float value, FillArea area, const FillRectFunc& fill_rect) const override;

    Color color_{48, 160, 220};
};

// Сегментный режим: шкала заполняется слева направо, но цвет каждого пикселя
// берется из диапазона, которому соответствует абсолютное значение этого пикселя.
class SegmentedLinearIndicator final : public LinearIndicatorBase {
public:
    using LinearIndicatorBase::LinearIndicatorBase;

private:
    void drawFill(float value, FillArea area, const FillRectFunc& fill_rect) const override;
};

// Слойный режим: каждый следующий диапазон снова заполняет шкалу с нуля на всю
// ширину и перекрывает предыдущий цвет. Это удобно для "недобор -> допуск ->
// перебор", когда текущий активный диапазон должен занимать максимум пикселей.
class OverlayLinearIndicator final : public LinearIndicatorBase {
public:
    using LinearIndicatorBase::LinearIndicatorBase;

    /**
     * @brief Включает сжатие уже пройденных диапазонов по высоте.
     * @details Когда значение перешло в следующий диапазон, предыдущие слои
     * рисуются неактивным цветом и могут занимать только центральную половину
     * высоты. Это оставляет их видимыми, но не смешивает с активным диапазоном.
     */
    void setCompressInactiveRanges(bool enabled);

private:
    void drawFill(float value, FillArea area, const FillRectFunc& fill_rect) const override;
    bool shouldDrawSetpoint(const Setpoint& setpoint, float value) const override;

    bool compress_inactive_ranges_ = false;
};

}  // namespace mixer::display
