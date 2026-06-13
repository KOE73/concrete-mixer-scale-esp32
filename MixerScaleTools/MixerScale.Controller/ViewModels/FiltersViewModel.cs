using System.Globalization;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Таблица MA-фильтров. Генерирует строки для ItemsControl.
/// Пересоздаётся при изменении состояния — таблица только для чтения, интерактивность не нужна.
/// </summary>
internal sealed partial class FiltersViewModel : ObservableObject
{
    [ObservableProperty]
    private IReadOnlyList<FilterRowViewModel> _rows = [];

    [ObservableProperty]
    private IReadOnlyList<string> _unitHeaders = ["raw"];

    public void Update(LiveWeightState? weight, DeviceSettingsState? settings)
    {
        if (weight is null)
        {
            Rows = [];
            UnitHeaders = ["raw"];
            return;
        }

        // Формируем заголовки колонок единиц измерения
        var headers = new List<string> { "raw" };
        headers.Add("kg");
        if (settings is not null)
        {
            headers.AddRange(settings.Units
                .Where(u => u.RawPerUnit > 0 && !string.IsNullOrWhiteSpace(u.Name) && !u.Name.Equals("kg", StringComparison.OrdinalIgnoreCase) && !u.Name.Equals("raw", StringComparison.OrdinalIgnoreCase))
                .Select(u => u.Name));
        }
        UnitHeaders = headers;

        // Строки таблицы — по одной на каждый MA-фильтр
        Rows = weight.Ma.Select(ma => new FilterRowViewModel
        {
            Name    = ma.Name,
            IsValid = ma.Valid,
            RawSum  = ma.Valid ? ma.RawSum.ToString() : "-",
            Kg      = FormatKg(ma, settings),
            Units   = FormatUnits(ma, settings)
        }).ToArray();
    }

    private static string FormatKg(MaState ma, DeviceSettingsState? settings)
    {
        if (!ma.Valid || settings is null)
        {
            return "-";
        }

        return ma.Weight.ToString("0.###", CultureInfo.InvariantCulture);
    }

    private static IReadOnlyList<string> FormatUnits(MaState ma, DeviceSettingsState? settings)
    {
        if (!ma.Valid || settings is null)
        {
            return [];
        }

        return settings.Units
            .Where(u => u.RawPerUnit > 0 && !string.IsNullOrWhiteSpace(u.Name) && !u.Name.Equals("kg", StringComparison.OrdinalIgnoreCase) && !u.Name.Equals("raw", StringComparison.OrdinalIgnoreCase))
            .Select(u =>
            {
                var value = (ma.RawSum - settings.SumOffset) / u.RawPerUnit;
                return value.ToString("0.###", CultureInfo.InvariantCulture);
            })
            .ToArray();
    }
}

internal sealed class FilterRowViewModel
{
    public string Name    { get; init; } = string.Empty;
    public bool   IsValid { get; init; }
    public string RawSum  { get; init; } = "-";
    public string Kg      { get; init; } = "-";
    public IReadOnlyList<string> Units { get; init; } = [];
}
