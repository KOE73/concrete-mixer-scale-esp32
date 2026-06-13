using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// ViewModel одной строки уставки.
/// Delta (отклонение от текущего raw) обновляется при каждом опросе,
/// Name и RawText — только при пересоздании строки или по команде SaveAsync.
/// </summary>
internal sealed partial class SetpointRowViewModel : ObservableObject
{
    private readonly Func<Task> _saveCallback;
    private readonly Func<Task> _deleteCallback;
    private ZeroSourceOption?   _lastZeroSource;

    [ObservableProperty] private string _name    = string.Empty;
    [ObservableProperty] private string _rawText = string.Empty;

    /// <summary>Разница текущего raw и raw уставки. Обновляется при каждом тике.</summary>
    [ObservableProperty] private string _deltaText = "-";

    public SetpointRowViewModel(
        SetpointState setpoint,
        ZeroSourceOption? zeroSource,
        Func<Task> saveCallback,
        Func<Task> deleteCallback)
    {
        _saveCallback   = saveCallback;
        _deleteCallback = deleteCallback;
        _lastZeroSource = zeroSource;

        _name    = setpoint.Name;
        _rawText = setpoint.RawValue.ToString(CultureInfo.InvariantCulture);
        _deltaText = CalcDelta(setpoint.RawValue, zeroSource);
    }

    public void UpdateData(SetpointState setpoint, ZeroSourceOption? zeroSource)
    {
        _lastZeroSource = zeroSource;
        // Обновляем только вычисляемое поле delta
        DeltaText = CalcDelta(setpoint.RawValue, zeroSource);
    }

    [RelayCommand]
    private async Task SaveAsync() => await _saveCallback();

    [RelayCommand]
    private async Task DeleteAsync() => await _deleteCallback();

    [RelayCommand]
    private void UseCurrent()
    {
        if (_lastZeroSource is { Valid: true } src)
        {
            RawText = src.RawSum.ToString(CultureInfo.InvariantCulture);
        }
    }

    private static string CalcDelta(long rawValue, ZeroSourceOption? zeroSource)
    {
        if (zeroSource is not { Valid: true })
        {
            return "-";
        }

        var delta = zeroSource.RawSum - rawValue;
        return delta.ToString("N0", CultureInfo.CurrentCulture);
    }
}
