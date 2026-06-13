using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using MixerScale.Controller.ViewModels;

namespace MixerScale.Controller.Views;

/// <summary>
/// Кастомный элемент отрисовки графика. Получает данные через свойства от GraphView.axaml.cs.
/// Не знает о ViewModel напрямую — вся передача данных через обычные свойства C#.
/// </summary>
internal sealed class GraphRenderControl : Control
{
    public IReadOnlyList<GraphSeries>  Series   { get; set; } = [];
    public IReadOnlyList<GraphMarker>  Markers  { get; set; } = [];
    public TimeSpan Duration { get; set; } = TimeSpan.FromSeconds(60);

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var bounds = Bounds;
        context.DrawRectangle(Brushes.White, new Pen(Brushes.LightGray, 1), bounds);

        var allPoints = Series.SelectMany(s => s.Points).ToArray();
        if (allPoints.Length == 0 && Markers.Count == 0)
        {
            return;
        }

        var now   = DateTimeOffset.UtcNow;
        var start = now - Duration;

        var values = allPoints.Select(p => p.Value)
            .Concat(Markers.Select(m => m.Value))
            .ToArray();

        var min = values.Min();
        var max = values.Max();
        if (Math.Abs(max - min) < 0.000001)
        {
            max += 1;
            min -= 1;
        }

        var plot = new Rect(
            bounds.X + 8,
            bounds.Y + 8,
            Math.Max(1, bounds.Width  - 16),
            Math.Max(1, bounds.Height - 16));

        // Горизонтальные линии сетки
        for (var i = 1; i < 4; ++i)
        {
            var y = plot.Top + plot.Height * i / 4.0;
            context.DrawLine(new Pen(Brushes.Gainsboro, 1),
                new Point(plot.Left, y), new Point(plot.Right, y));
        }

        // Горизонтальные маркеры уставок
        foreach (var marker in Markers)
        {
            var yRatio = (marker.Value - min) / (max - min);
            var y      = plot.Bottom - yRatio * plot.Height;
            context.DrawLine(new Pen(Brushes.IndianRed, 1),
                new Point(plot.Left, y), new Point(plot.Right, y));
            context.DrawText(
                new FormattedText(
                    $"{marker.Name} {marker.Value:0.###}",
                    CultureInfo.CurrentCulture,
                    FlowDirection.LeftToRight,
                    Typeface.Default,
                    12,
                    Brushes.IndianRed),
                new Point(plot.Left + 6, Math.Max(plot.Top, y - 16)));
        }

        // Линии серий
        foreach (var series in Series)
        {
            var brush = new SolidColorBrush(series.Color);
            var pen   = new Pen(brush, 2);
            Point? previous = null;
            foreach (var point in series.Points.Where(p => p.Time >= start))
            {
                var xRatio  = (point.Time - start).TotalMilliseconds / Math.Max(1, Duration.TotalMilliseconds);
                var yRatio  = (point.Value - min) / (max - min);
                var current = new Point(
                    plot.Left + xRatio * plot.Width,
                    plot.Bottom - yRatio * plot.Height);
                if (previous is not null)
                {
                    context.DrawLine(pen, previous.Value, current);
                }
                previous = current;
            }
        }
    }
}
