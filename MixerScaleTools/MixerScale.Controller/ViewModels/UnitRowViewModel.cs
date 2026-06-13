using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// ViewModel одной строки в списке единиц измерения.
/// Хранит редактируемые поля; колбэки Save/Delete ведут в UnitsViewModel.
/// При обновлении данных (UpdateData) меняет только вычисляемые поля, не трогая то что редактирует пользователь.
/// </summary>
internal sealed partial class UnitRowViewModel : ObservableObject
{
    private readonly Func<Task> _saveCallback;
    private readonly Func<Task> _deleteCallback;

    // Редактируемые поля (пользователь вводит текст)
    [ObservableProperty] private string _name    = string.Empty;
    [ObservableProperty] private string _rawText = string.Empty;

    // Только для чтения — текущее значение веса в этой единице
    [ObservableProperty] private string _currentValueText = "-";



    public UnitRowViewModel(
        UnitConversionState unit,
        int index,
        ZeroSourceOption? zeroSource,
        DeviceSettingsState settings,
        Func<Task> saveCallback,
        Func<Task> deleteCallback)
    {
        _saveCallback   = saveCallback;
        _deleteCallback = deleteCallback;

        // Инициализируем редактируемые поля один раз при создании строки
        _name    = unit.Name;
        _rawText = unit.RawPerUnit.ToString("0.###", CultureInfo.InvariantCulture);
        _currentValueText = CalcCurrentValue(unit, zeroSource, settings);
    }

    /// <summary>
    /// Вызывается при каждом обновлении состояния. Обновляет только вычисляемое поле,
    /// чтобы не перебивать ввод пользователя в Name и RawText.
    /// </summary>
    public void UpdateData(UnitConversionState unit, ZeroSourceOption? zeroSource, DeviceSettingsState settings)
    {
        CurrentValueText = CalcCurrentValue(unit, zeroSource, settings);
    }

    [RelayCommand]
    private async Task SaveAsync() => await _saveCallback();

    [RelayCommand]
    private async Task DeleteAsync() => await _deleteCallback();

    private static string CalcCurrentValue(
        UnitConversionState unit,
        ZeroSourceOption? zeroSource,
        DeviceSettingsState settings)
    {
        if (zeroSource is not { Valid: true } || unit.RawPerUnit <= 0)
        {
            return "-";
        }

        var value = (zeroSource.RawSum - settings.SumOffset) / unit.RawPerUnit;
        return value.ToString("N0", CultureInfo.CurrentCulture) + " " + unit.Name;
    }
}
