using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Media;

namespace MixerScale.Controller.Views;

public class IndicatorBarControl : Control
{
    // === Dependency Properties ===

    public static readonly StyledProperty<double> MinimumProperty =
        AvaloniaProperty.Register<IndicatorBarControl, double>(nameof(Minimum));

    public static readonly StyledProperty<double> MaximumProperty =
        AvaloniaProperty.Register<IndicatorBarControl, double>(nameof(Maximum), 100);

    public static readonly StyledProperty<double> SetpointProperty =
        AvaloniaProperty.Register<IndicatorBarControl, double>(nameof(Setpoint), 80);

    public static readonly StyledProperty<double> ValueProperty =
        AvaloniaProperty.Register<IndicatorBarControl, double>(nameof(Value));

    public static readonly StyledProperty<double> MagnifierRangeRatioProperty =
        AvaloniaProperty.Register<IndicatorBarControl, double>(nameof(MagnifierRangeRatio), 0.2);

    public static readonly StyledProperty<IBrush> FillBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(FillBrush), new SolidColorBrush(Colors.Blue));

    public static readonly StyledProperty<IBrush> BackgroundBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(BackgroundBrush), new SolidColorBrush(Colors.LightGray));

    public static readonly StyledProperty<IBrush> SetpointLineBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(SetpointLineBrush), new SolidColorBrush(Colors.Black));

    public static readonly StyledProperty<int> TrafficLightStateProperty =
        AvaloniaProperty.Register<IndicatorBarControl, int>(nameof(TrafficLightState));

    public static readonly StyledProperty<IBrush> GreenBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(GreenBrush), new SolidColorBrush(Colors.Green));

    public static readonly StyledProperty<IBrush> YellowBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(YellowBrush), new SolidColorBrush(Colors.Yellow));

    public static readonly StyledProperty<IBrush> RedBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(RedBrush), new SolidColorBrush(Colors.Red));

    public static readonly StyledProperty<bool> IsOverfillProperty =
        AvaloniaProperty.Register<IndicatorBarControl, bool>(nameof(IsOverfill));

    public static readonly StyledProperty<IBrush> OverfillBrushProperty =
        AvaloniaProperty.Register<IndicatorBarControl, IBrush>(nameof(OverfillBrush), new SolidColorBrush(Colors.Red));

    public double Minimum
    {
        get => GetValue(MinimumProperty);
        set => SetValue(MinimumProperty, value);
    }

    public double Maximum
    {
        get => GetValue(MaximumProperty);
        set => SetValue(MaximumProperty, value);
    }

    public double Setpoint
    {
        get => GetValue(SetpointProperty);
        set => SetValue(SetpointProperty, value);
    }

    public double Value
    {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public double MagnifierRangeRatio
    {
        get => GetValue(MagnifierRangeRatioProperty);
        set => SetValue(MagnifierRangeRatioProperty, value);
    }

    public IBrush FillBrush
    {
        get => GetValue(FillBrushProperty);
        set => SetValue(FillBrushProperty, value);
    }

    public IBrush BackgroundBrush
    {
        get => GetValue(BackgroundBrushProperty);
        set => SetValue(BackgroundBrushProperty, value);
    }

    public IBrush SetpointLineBrush
    {
        get => GetValue(SetpointLineBrushProperty);
        set => SetValue(SetpointLineBrushProperty, value);
    }

    public int TrafficLightState
    {
        get => GetValue(TrafficLightStateProperty);
        set => SetValue(TrafficLightStateProperty, value);
    }

    public IBrush GreenBrush
    {
        get => GetValue(GreenBrushProperty);
        set => SetValue(GreenBrushProperty, value);
    }

    public IBrush YellowBrush
    {
        get => GetValue(YellowBrushProperty);
        set => SetValue(YellowBrushProperty, value);
    }

    public IBrush RedBrush
    {
        get => GetValue(RedBrushProperty);
        set => SetValue(RedBrushProperty, value);
    }

    public bool IsOverfill
    {
        get => GetValue(IsOverfillProperty);
        set => SetValue(IsOverfillProperty, value);
    }

    public IBrush OverfillBrush
    {
        get => GetValue(OverfillBrushProperty);
        set => SetValue(OverfillBrushProperty, value);
    }

    static IndicatorBarControl()
    {
        AffectsRender<IndicatorBarControl>(
            MinimumProperty, MaximumProperty, SetpointProperty, ValueProperty, 
            MagnifierRangeRatioProperty, FillBrushProperty, BackgroundBrushProperty, SetpointLineBrushProperty,
            TrafficLightStateProperty, GreenBrushProperty, YellowBrushProperty, RedBrushProperty, IsOverfillProperty, OverfillBrushProperty);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        double lightRadius = 32;
        double lightMargin = 16;
        double barTop = lightRadius * 2 + lightMargin;
        var barBounds = new Rect(0, barTop, Bounds.Width, Bounds.Height - barTop);

        // 1. Draw Traffic Light
        var lightBrush = TrafficLightState switch
        {
            1 => YellowBrush,
            2 => RedBrush,
            _ => GreenBrush
        };
        var lightCenter = new Point(Bounds.Width / 2, lightRadius);
        context.DrawEllipse(lightBrush, new Pen(SetpointLineBrush, 8), lightCenter, lightRadius, lightRadius);

        // 2. Draw Background
        context.FillRectangle(BackgroundBrush, barBounds);

        double totalRange = Maximum - Minimum;
        if (totalRange <= 0) return;

        double magnifierRange = Maximum - Setpoint;
        
        // The magnifier region represents [Setpoint - magnifierRange, Setpoint + magnifierRange]
        // But wait! According to the logic:
        // Top 1/3 visual height is the magnifier.
        // Setpoint is in the middle of Top 1/3 (i.e. at 5/6 total height from bottom).
        // Bottom 2/3 visual height maps to [Minimum, Setpoint - magnifierRange].
        
        double bottomHeightRatio = 2.0 / 3.0;
        double magnifierHeightRatio = 1.0 / 3.0;
        
        double yBottomSectionTop = barBounds.Y + barBounds.Height * (1.0 - bottomHeightRatio); 
        double ySetpoint = barBounds.Y + barBounds.Height * (1.0 - (bottomHeightRatio + magnifierHeightRatio / 2.0));
        
        double valueStartMagnifier = Setpoint - magnifierRange;
        double valueEndMagnifier = Setpoint + magnifierRange;

        double fillHeight = 0;

        if (Value <= Minimum)
        {
            fillHeight = 0;
        }
        else if (Value <= valueStartMagnifier)
        {
            // Linear map in the bottom section
            double ratio = (Value - Minimum) / Math.Max(1, valueStartMagnifier - Minimum);
            fillHeight = barBounds.Height * bottomHeightRatio * ratio;
        }
        else if (Value <= valueEndMagnifier)
        {
            // Fully fill the bottom section
            fillHeight = barBounds.Height * bottomHeightRatio;
            // Linear map in the magnifier section
            double ratio = (Value - valueStartMagnifier) / (2.0 * magnifierRange);
            fillHeight += barBounds.Height * magnifierHeightRatio * ratio;
        }
        else
        {
            // Value is above the magnifier range, maybe clip to 100%
            fillHeight = barBounds.Height;
        }

        // 2. Draw Fill (growing from bottom to top)
        if (fillHeight > 0)
        {
            var fillRect = new Rect(barBounds.X, barBounds.Bottom - fillHeight, barBounds.Width, fillHeight);
            context.FillRectangle(IsOverfill ? OverfillBrush : FillBrush, fillRect);
        }

        // 3. Draw Setpoint Line
        var setpointLinePen = new Pen(SetpointLineBrush, 8);
        context.DrawLine(setpointLinePen, new Point(barBounds.X, ySetpoint), new Point(barBounds.Right, ySetpoint));
        
        // 4. Draw separator between bottom section and magnifier
        var separatorPen = new Pen(SetpointLineBrush, 4) { DashStyle = DashStyle.Dash };
        context.DrawLine(separatorPen, new Point(barBounds.X, yBottomSectionTop), new Point(barBounds.Right, yBottomSectionTop));
    }
}
