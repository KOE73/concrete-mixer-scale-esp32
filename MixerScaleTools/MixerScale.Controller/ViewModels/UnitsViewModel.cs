using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Models;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Список единиц измерения. Использует стабильные строки:
/// пересоздаёт коллекцию только при изменении количества строк,
/// иначе обновляет данные в уже существующих UnitRowViewModel — фокус в полях не теряется.
/// </summary>
internal sealed partial class UnitsViewModel : ObservableObject
{
    private readonly IMixerScaleService _service;
    private readonly CalibrationViewModel _calibration;

    [ObservableProperty] private ObservableCollection<UnitRowViewModel> _rows = [];
    [ObservableProperty] private string _newName = string.Empty;
    [ObservableProperty] private string _newRaw  = string.Empty;
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStatusText))]
    private string _statusText = string.Empty;

    public bool HasStatusText => !string.IsNullOrEmpty(StatusText);

    private void SetStatus(string msg) => StatusText = msg;

    public UnitsViewModel(IMixerScaleService service, CalibrationViewModel calibration)
    {
        _service     = service;
        _calibration = calibration;
    }

    public void Update(DeviceSettingsState? settings, ZeroSourceOption? zeroSource)
    {
        if (settings is null)
        {
            Rows.Clear();
            return;
        }

        var units = settings.Units;

        // Пересоздаём строки только если изменилось количество
        if (units.Count != Rows.Count)
        {
            Rows.Clear();
            for (var i = 0; i < units.Count; i++)
            {
                var idx = i; // захват для замыкания
                Rows.Add(new UnitRowViewModel(
                    units[idx],
                    idx,
                    zeroSource,
                    settings,
                    saveCallback:   () => SaveRowAsync(idx),
                    deleteCallback: () => DeleteAsync(idx)));
            }
        }
        else
        {
            // Обновляем данные в уже существующих строках без пересоздания
            for (var i = 0; i < units.Count; i++)
            {
                Rows[i].UpdateData(units[i], zeroSource, settings);
            }
        }
    }

    [RelayCommand]
    private async Task AddAsync()
    {
        if (!_calibration.TryReadForm(out var settings)) return;

        var name = NewName.Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            SetStatus("Введите единицу измерения.");
            return;
        }
        if (name.Length > 5) name = name[..5];
        if (!name.All(c => (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
        {
            SetStatus("Единица измерения: только английские буквы.");
            return;
        }
        if (!CalibrationViewModel.TryParseDouble(NewRaw, out var rawPerUnit) || rawPerUnit <= 0)
        {
            SetStatus("raw/ед. должен быть положительным числом.");
            return;
        }
        if (settings.Units.Count >= 8)
        {
            SetStatus("Максимум 8 единиц.");
            return;
        }

        var updated = settings with
        {
            Units = [.. settings.Units, new UnitConversionState { Name = name, RawPerUnit = rawPerUnit }]
        };

        if (await _calibration.SaveAsync(updated, $"Добавлена единица {name}.", SetStatus))
        {
            NewName = string.Empty;
            NewRaw  = string.Empty;
        }
    }

    private async Task SaveRowAsync(int index)
    {
        if (!_calibration.TryReadForm(out var settings)) return;
        if (index < 0 || index >= Rows.Count) return;

        var row = Rows[index];
        var name = row.Name.Trim();
        if (name.Length > 5) name = name[..5];
        if (!name.All(c => (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
        {
            SetStatus("Единица измерения: только английские буквы.");
            return;
        }
        if (!CalibrationViewModel.TryParseDouble(row.RawText, out var rawPerUnit) || rawPerUnit <= 0)
        {
            SetStatus("raw/ед. должен быть положительным числом.");
            return;
        }

        var units = settings.Units.ToList();
        units[index] = units[index] with { Name = name, RawPerUnit = rawPerUnit };
        await _calibration.SaveAsync(settings with { Units = units }, $"Единица {name} сохранена.", SetStatus);
    }

    private async Task DeleteAsync(int index)
    {
        if (!_calibration.TryReadForm(out var settings)) return;
        if (index < 0 || index >= settings.Units.Count) return;

        var removed = settings.Units[index].Name;
        var units   = settings.Units.ToList();
        units.RemoveAt(index);
        await _calibration.SaveAsync(settings with { Units = units }, $"Удалена единица {removed}.", SetStatus);
    }
}
