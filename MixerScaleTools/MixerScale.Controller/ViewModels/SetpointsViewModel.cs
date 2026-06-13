using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Models;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Список уставок. Использует стабильные строки — аналогично UnitsViewModel.
/// Строки пересоздаются только при изменении их количества.
/// </summary>
internal sealed partial class SetpointsViewModel : ObservableObject
{
    private readonly IMixerScaleService _service;
    private readonly CalibrationViewModel _calibration;

    [ObservableProperty] private ObservableCollection<SetpointRowViewModel> _rows = [];
    [ObservableProperty] private string _newName = string.Empty;
    [ObservableProperty] private string _newRaw  = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStatusText))]
    private string _statusText = string.Empty;

    public bool HasStatusText => !string.IsNullOrEmpty(StatusText);

    private void SetStatus(string msg) => StatusText = msg;

    private ZeroSourceOption? _lastZeroSource;

    public SetpointsViewModel(IMixerScaleService service, CalibrationViewModel calibration)
    {
        _service     = service;
        _calibration = calibration;
    }

    public void Update(DeviceSettingsState? settings, ZeroSourceOption? zeroSource)
    {
        _lastZeroSource = zeroSource;

        if (settings is null)
        {
            Rows.Clear();
            return;
        }

        var setpoints = settings.Setpoints;

        if (setpoints.Count != Rows.Count)
        {
            Rows.Clear();
            for (var i = 0; i < setpoints.Count; i++)
            {
                var idx = i;
                Rows.Add(new SetpointRowViewModel(
                    setpoints[idx],
                    zeroSource,
                    saveCallback:   () => SaveRowAsync(idx),
                    deleteCallback: () => DeleteAsync(idx)));
            }
        }
        else
        {
            for (var i = 0; i < setpoints.Count; i++)
            {
                Rows[i].UpdateData(setpoints[i], zeroSource);
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
            SetStatus("Введите имя уставки.");
            return;
        }
        if (!CalibrationViewModel.TryParseLong(NewRaw, out var rawValue))
        {
            SetStatus("raw уставки должен быть целым числом.");
            return;
        }
        if (settings.Setpoints.Count >= 16)
        {
            SetStatus("Максимум 16 уставок.");
            return;
        }

        var sp = new SetpointState
        {
            Name     = name.Length > 24 ? name[..24] : name,
            RawValue = rawValue
        };
        var updated = settings with { Setpoints = [.. settings.Setpoints, sp] };

        if (await _calibration.SaveAsync(updated, $"Добавлена уставка {name}.", SetStatus))
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
        if (string.IsNullOrWhiteSpace(name))
        {
            SetStatus("Введите имя уставки.");
            return;
        }
        if (!CalibrationViewModel.TryParseLong(row.RawText, out var rawValue))
        {
            SetStatus("raw уставки должен быть целым числом.");
            return;
        }

        var setpoints = settings.Setpoints.ToList();
        setpoints[index] = setpoints[index] with
        {
            Name     = name.Length > 24 ? name[..24] : name,
            RawValue = rawValue
        };
        await _calibration.SaveAsync(settings with { Setpoints = setpoints }, $"Уставка {name} сохранена.", SetStatus);
    }

    private async Task DeleteAsync(int index)
    {
        if (!_calibration.TryReadForm(out var settings)) return;
        if (index < 0 || index >= settings.Setpoints.Count) return;

        var removed   = settings.Setpoints[index].Name;
        var setpoints = settings.Setpoints.ToList();
        setpoints.RemoveAt(index);
        await _calibration.SaveAsync(settings with { Setpoints = setpoints }, $"Удалена уставка {removed}.", SetStatus);
    }

    [RelayCommand]
    private void UseCurrentNew()
    {
        if (_lastZeroSource is { Valid: true } src)
        {
            NewRaw = src.RawSum.ToString(CultureInfo.InvariantCulture);
        }
    }
}
