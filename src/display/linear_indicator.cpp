#include "display/linear_indicator.hpp"

#include <algorithm>
#include <cmath>

namespace mixer::display {

#pragma region LinearIndicatorBase

LinearIndicatorBase::LinearIndicatorBase(int x, int y, int width, int height)
    : x_(x), y_(y), width_(width), height_(height) {}

void LinearIndicatorBase::setValueRange(float minimum, float maximum) {
    // Нулевой или перевернутый диапазон превращаем в минимально валидный, чтобы
    // расчеты пикселей дальше не делили на ноль.
    minimum_ = minimum;
    maximum_ = maximum > minimum ? maximum : minimum + 1.0f;
}

void LinearIndicatorBase::setFrame(bool enabled, Color color) {
    frame_enabled_ = enabled;
    frame_color_ = color;
}

void LinearIndicatorBase::setFillBounds(FillBounds bounds) {
    // Этот флаг нужен именно для маленьких матриц: один пиксель рамки может быть
    // либо визуальной границей, либо частью полезной шкалы.
    fill_bounds_ = bounds;
}

void LinearIndicatorBase::setDirection(Direction direction) {
    direction_ = direction;
}

void LinearIndicatorBase::clearRanges() {
    range_count_ = 0;
}

bool LinearIndicatorBase::addRange(float from, float to, Color color) {
    return addRange(from, to, color, color);
}

bool LinearIndicatorBase::addRange(float from, float to, Color color1, Color color2) {
    // Диапазоны должны идти с нормальной шириной. Пересечения не запрещаем:
    // порядок диапазонов может иметь смысл для конкретного способа отрисовки.
    if (range_count_ >= kMaxRanges || to <= from) {
        return false;
    }

    ranges_[range_count_++] = {from, to, color1, color2};
    return true;
}

void LinearIndicatorBase::clearSetpoints() {
    setpoint_count_ = 0;
}

bool LinearIndicatorBase::addSetpoint(float value, Color color) {
    if (setpoint_count_ >= kMaxSetpoints) {
        return false;
    }

    setpoints_[setpoint_count_++] = {value, color};
    return true;
}

void LinearIndicatorBase::draw(float value,
                               const FillRectFunc& fill_rect,
                               const DrawPixelFunc& draw_pixel) const {
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    fill_rect(x_, y_, width_, height_, {0, 0, 0});
    drawFrame(fill_rect);

    const FillArea area = fillArea();
    if (area.width <= 0 || area.height <= 0) {
        return;
    }

    // Потомок рисует только заполнение. Рамка, очистка и уставки остаются в базе,
    // чтобы разные режимы шкалы не расходились в мелких правилах.
    drawFill(clampedValue(value), area, fill_rect);
    drawSetpoints(clampedValue(value), area, draw_pixel);
}

bool LinearIndicatorBase::shouldDrawSetpoint(const Setpoint&, float) const {
    return true;
}

int LinearIndicatorBase::valueToX(float value, FillArea area) const {
    // Обычный перевод абсолютного значения в координату общей шкалы.
    return area.x + valueToOffset(value, area);
}

int LinearIndicatorBase::valueToOffset(float value, FillArea area) const {
    const float range = std::max(maximum_ - minimum_, 1.0f);
    const float ratio = std::clamp((value - minimum_) / range, 0.0f, 1.0f);
    return static_cast<int>(std::round(ratio * static_cast<float>(fillLength(area) - 1)));
}

int LinearIndicatorBase::widthToValue(float value, FillArea area) const {
    // Возвращает количество закрашиваемых пикселей для общей шкалы.
    return std::clamp(valueToOffset(value, area) + 1, 0, fillLength(area));
}

float LinearIndicatorBase::valueAtOffset(int offset, FillArea area) const {
    // Обратный перевод пикселя в значение нужен сегментной шкале, где цвет
    // каждого пикселя выбирается по диапазону значения.
    const int length = fillLength(area);
    const float ratio = length > 1
                            ? static_cast<float>(offset) / static_cast<float>(length - 1)
                            : 0.0f;
    return minimum_ + ratio * (maximum_ - minimum_);
}

int LinearIndicatorBase::fillLength(FillArea area) const {
    return isVertical() ? area.height : area.width;
}

bool LinearIndicatorBase::isVertical() const {
    return direction_ == Direction::Vertical;
}

void LinearIndicatorBase::fillFromStart(int filled,
                                        FillArea area,
                                        Color color,
                                        const FillRectFunc& fill_rect) const {
    const int safe_filled = std::clamp(filled, 0, fillLength(area));
    if (safe_filled <= 0) {
        return;
    }

    if (isVertical()) {
        fill_rect(area.x,
                  area.y + area.height - safe_filled,
                  area.width,
                  safe_filled,
                  color);
        return;
    }

    fill_rect(area.x, area.y, safe_filled, area.height, color);
}

void LinearIndicatorBase::fillAtOffset(int offset,
                                       FillArea area,
                                       Color color,
                                       const FillRectFunc& fill_rect) const {
    if (isVertical()) {
        fill_rect(area.x, area.y + area.height - 1 - offset, area.width, 1, color);
        return;
    }

    fill_rect(area.x + offset, area.y, 1, area.height, color);
}

float LinearIndicatorBase::clampedValue(float value) const {
    return std::clamp(value, minimum_, maximum_);
}

const std::array<LinearIndicatorBase::Range, LinearIndicatorBase::kMaxRanges>&
LinearIndicatorBase::ranges() const {
    return ranges_;
}

std::size_t LinearIndicatorBase::rangeCount() const {
    return range_count_;
}

LinearIndicatorBase::Color LinearIndicatorBase::colorForValue(float value) const {
    // При пересекающихся диапазонах выигрывает первый подходящий. Для текущих
    // настроек диапазоны не пересекаются, но правило делает поведение явным.
    for (std::size_t i = 0; i < range_count_; ++i) {
        if (value >= ranges_[i].from && value <= ranges_[i].to) {
            return ranges_[i].color1;
        }
    }

    return range_count_ > 0 ? ranges_[range_count_ - 1].color1 : Color{48, 160, 220};
}

LinearIndicatorBase::FillArea LinearIndicatorBase::fillArea() const {
    // IncludeFrame отдает потомку всю геометрию, InsideFrame оставляет один
    // пиксель под рамку по периметру.
    const bool include_frame = fill_bounds_ == FillBounds::IncludeFrame || !frame_enabled_;
    return {
        include_frame ? x_ : x_ + 1,
        include_frame ? y_ : y_ + 1,
        include_frame ? width_ : width_ - 2,
        include_frame ? height_ : height_ - 2,
    };
}

void LinearIndicatorBase::drawFrame(const FillRectFunc& fill_rect) const {
    if (!frame_enabled_) {
        return;
    }

    fill_rect(x_, y_, width_, 1, frame_color_);
    fill_rect(x_, y_ + height_ - 1, width_, 1, frame_color_);
    fill_rect(x_, y_, 1, height_, frame_color_);
    fill_rect(x_ + width_ - 1, y_, 1, height_, frame_color_);
}

void LinearIndicatorBase::drawSetpoints(float value,
                                        FillArea area,
                                        const DrawPixelFunc& draw_pixel) const {
    for (std::size_t i = 0; i < setpoint_count_; ++i) {
        const Setpoint& setpoint = setpoints_[i];
        if (!shouldDrawSetpoint(setpoint, value)) {
            continue;
        }

        const int marker_offset = valueToOffset(setpoint.value, area);
        const int cross_length = isVertical() ? area.width : area.height;
        for (int offset = 0; offset < cross_length; ++offset) {
            // Маркер повторяет "цвет уставки / черный / пропуск". На HUB75 это
            // заметнее, чем одноцветная пунктирная линия, и не превращает шкалу
            // в сплошную белую стенку.
            if ((offset % 3) == 2) {
                continue;
            }

            const Color color = (offset % 3) == 0 ? setpoint.color : Color{0, 0, 0};
            const int x = isVertical() ? area.x + offset : area.x + marker_offset;
            const int y = isVertical()
                              ? area.y + area.height - 1 - marker_offset
                              : area.y + offset;
            draw_pixel(x, y, color);
        }
    }
}

#pragma endregion

#pragma region SolidLinearIndicator

void SolidLinearIndicator::setColor(Color color) {
    color_ = color;
}

void SolidLinearIndicator::drawFill(float value,
                                    FillArea area,
                                    const FillRectFunc& fill_rect) const {
    // Простая линейная шкала: количество пикселей пропорционально общему
    // диапазону индикатора, цвет не меняется.
    const int filled = widthToValue(value, area);
    fillFromStart(filled, area, color_, fill_rect);
}

#pragma endregion

#pragma region SegmentedLinearIndicator

void SegmentedLinearIndicator::drawFill(float value,
                                        FillArea area,
                                        const FillRectFunc& fill_rect) const {
    // Сегменты существуют одновременно: каждый пиксель получает цвет диапазона,
    // которому соответствует его абсолютное значение на общей шкале.
    const int filled = widthToValue(value, area);
    for (int dx = 0; dx < filled; ++dx) {
        fillAtOffset(dx, area, colorForValue(valueAtOffset(dx, area)), fill_rect);
    }
}

#pragma endregion

#pragma region OverlayLinearIndicator

void OverlayLinearIndicator::drawFill(float value,
                                      FillArea area,
                                      const FillRectFunc& fill_rect) const {
    // Каждый диапазон рисуется как отдельный слой на всю ширину. Когда значение
    // входит в следующий диапазон, новый цвет снова растет от левого края и
    // перекрывает предыдущий слой.
    for (std::size_t i = 0; i < rangeCount(); ++i) {
        const Range& range = ranges()[i];
        if (value < range.from) {
            continue;
        }

        const float visible_end = std::min(value, range.to);
        const float range_span = std::max(range.to - range.from, 1.0f);
        const float range_progress = std::clamp((visible_end - range.from) / range_span, 0.0f, 1.0f);
        const int filled = std::clamp(
            static_cast<int>(std::round(range_progress * static_cast<float>(fillLength(area)))),
            0,
            fillLength(area));
        if (filled > 0) {
            const bool inactive = value > range.to;
            const Color color = inactive ? range.color2 : range.color1;
            if (inactive && compress_inactive_ranges_) {
                if (isVertical()) {
                    const int inactive_width = std::max(area.width / 2, 1);
                    fill_rect(area.x + (area.width - inactive_width) / 2,
                              area.y + area.height - filled,
                              inactive_width,
                              filled,
                              color);
                } else {
                    const int inactive_height = std::max(area.height / 2, 1);
                    fill_rect(area.x,
                              area.y + (area.height - inactive_height) / 2,
                              filled,
                              inactive_height,
                              color);
                }
            } else {
                fillFromStart(filled, area, color, fill_rect);
            }
        }
    }
}

bool OverlayLinearIndicator::shouldDrawSetpoint(const Setpoint& setpoint, float value) const {
    // В слойном режиме уставка появляется сразу при входе значения в диапазон,
    // к которому эта уставка относится. Саму уставку еще можно не достичь, но
    // оператор уже видит будущую цель активного слоя.
    for (std::size_t i = 0; i < rangeCount(); ++i) {
        const Range& range = ranges()[i];
        if (setpoint.value >= range.from && setpoint.value <= range.to) {
            return value >= range.from;
        }
    }

    return value >= setpoint.value;
}

void OverlayLinearIndicator::setCompressInactiveRanges(bool enabled) {
    compress_inactive_ranges_ = enabled;
}

#pragma endregion

}  // namespace mixer::display
