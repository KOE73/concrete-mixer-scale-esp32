using System.Collections.ObjectModel;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;

namespace MixerScale.Controller.ViewModels;

internal sealed partial class IndicatorViewModel : ObservableObject
{
    [ObservableProperty] private string _name = string.Empty;
    [ObservableProperty] private double _minimum;
    [ObservableProperty] private double _maximum;
    [ObservableProperty] private double _setpoint;
    [ObservableProperty] private double _value;
    
    // Светофор: 0 - Выключен/Green(Нижняя часть), 1 - Yellow(В лупе), 2 - Red(Уставка)
    [ObservableProperty] private int _trafficLightState;
    [ObservableProperty] private bool _isOverfill;

    public void UpdateState()
    {
        double magnifierRange = (Maximum - Minimum) * 0.2; // 20%
        
        if (Value >= Setpoint)
        {
            TrafficLightState = 2; // Red (Stop)
            IsOverfill = true;
        }
        else if (Value >= Setpoint - magnifierRange)
        {
            TrafficLightState = 1; // Yellow (Soon)
            IsOverfill = false;
        }
        else
        {
            TrafficLightState = 0; // Green (Throw)
            IsOverfill = false;
        }
    }
}
