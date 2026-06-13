using System.Collections.ObjectModel;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Styling;
using CommunityToolkit.Mvvm.ComponentModel;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.ViewModels;

internal sealed partial class DashboardViewModel : ObservableObject
{
    public ObservableCollection<IndicatorViewModel> Indicators { get; } = new();

    public void UpdateSetpoints(IReadOnlyList<SetpointState> setpoints)
    {
        if (Indicators.Count != setpoints.Count || setpoints.Count == 0)
        {
            Indicators.Clear();
            foreach (var sp in setpoints)
            {
                Indicators.Add(new IndicatorViewModel { Name = sp.Name });
            }
        }

        long currentMin = 0;
        for (int i = 0; i < setpoints.Count; i++)
        {
            var sp = setpoints[i];
            var ind = Indicators[i];
            
            ind.Name = sp.Name;
            ind.Minimum = currentMin;
            ind.Setpoint = sp.RawValue;
            
            // Maximum is setpoint + 20% of range, but visually magnifier is 1/3
            // According to math: Setpoint is in the middle of top 1/3.
            // So if magnifierRange = 20% of total visual range,
            // Maximum = Setpoint + MagnifierRange.
            double range = Math.Max(1, sp.RawValue - currentMin);
            // Увеличиваем чувствительность верхней трети (лупы):
            // Пусть лупа охватывает только последние 5% веса
            double magnifierRange = Math.Max(2.0, range * 0.05); // 5% (но не менее 2 единиц)
            
            ind.Maximum = sp.RawValue + magnifierRange;
            ind.UpdateState();

            currentMin = sp.RawValue;
        }
    }

    public void UpdateLiveWeight(LiveWeightState weight)
    {
        var primaryMa = weight.Ma.FirstOrDefault(m => m.Name == "ma_3s")
                     ?? weight.Ma.FirstOrDefault(m => m.Name.StartsWith("ma_", StringComparison.OrdinalIgnoreCase));
                     
        double currentValue = primaryMa?.RawSum ?? weight.RawSum;

        foreach (var ind in Indicators)
        {
            ind.Value = currentValue;
            ind.UpdateState();
        }
    }
}
