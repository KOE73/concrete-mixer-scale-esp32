using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using MixerScale.Controller.Models;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// ViewModel окна эмулятора. Управляет RawSum через IEmulatorControl
/// и отображает текущие значения ячеек единиц и уставок.
/// </summary>
internal sealed partial class EmulatorViewModel : ObservableObject
{
    private readonly IEmulatorControl _control;

    public string Title { get; }
    public long RawMin => _control.RawMin;
    public long RawMax => _control.RawMax;

    [ObservableProperty]
    private long _rawSum;

    [ObservableProperty]
    private string _rawSumDisplay = "0";

    [ObservableProperty]
    private IReadOnlyList<EmulatorUnitDisplay> _unitDisplays = [];

    [ObservableProperty]
    private IReadOnlyList<EmulatorSetpointDisplay> _setpointDisplays = [];

    public EmulatorViewModel(IEmulatorControl control, string mixerName)
    {
        _control = control;
        Title    = $"Эмулятор: {mixerName}";
        _rawSum  = control.RawSum;

        // Подписываемся на StateUpdated если контрол это поддерживает
        if (control is IMixerScaleService service)
        {
            service.StateUpdated += OnStateUpdated;
        }
    }

    partial void OnRawSumChanged(long value)
    {
        _control.RawSum  = value;
        RawSumDisplay    = value.ToString(CultureInfo.InvariantCulture);
        RefreshDisplays(_control is IMixerScaleService svc ? svc.LastSettings : null);
    }

    private void OnStateUpdated()
    {
        var settings = _control is IMixerScaleService svc ? svc.LastSettings : null;
        RawSum = _control.RawSum;
        RefreshDisplays(settings);
    }

    private void RefreshDisplays(DeviceSettingsState? settings)
    {
        if (settings is null)
        {
            UnitDisplays     = [];
            SetpointDisplays = [];
            return;
        }

        var raw = _control.RawSum;

        UnitDisplays = settings.Units
            .Where(u => u.RawPerUnit > 0 && !string.IsNullOrWhiteSpace(u.Name))
            .Select(u =>
            {
                var value = (raw - settings.SumOffset) / u.RawPerUnit;
                return new EmulatorUnitDisplay(u.Name, $"{value:0.###} {u.Name}");
            })
            .ToArray();

        SetpointDisplays = settings.Setpoints
            .Where(s => !string.IsNullOrWhiteSpace(s.Name))
            .Select(s =>
            {
                var delta = raw - s.RawValue;
                return new EmulatorSetpointDisplay(s.Name, s.RawValue.ToString(), delta.ToString());
            })
            .ToArray();
    }
}

internal sealed record EmulatorUnitDisplay(string Name, string Value);
internal sealed record EmulatorSetpointDisplay(string Name, string Raw, string Delta);
