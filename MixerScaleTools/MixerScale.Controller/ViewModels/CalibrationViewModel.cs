using System.Globalization;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Models;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Секция калибровки: offset/scale, zero, управление единицами и уставками.
/// Единственная точка, через которую идут команды записи на устройство.
/// </summary>
internal sealed partial class CalibrationViewModel : ObservableObject
{
    private readonly IMixerScaleService _service;

    private DeviceSettingsState? _currentSettings;
    private long _currentRawSum;

    // --- Calibration fields ---

    [ObservableProperty] private string _offsetInput = string.Empty;
    [ObservableProperty] private string _scaleInput  = string.Empty;
    [ObservableProperty] private string _currentOffset = "—";
    [ObservableProperty] private string _currentScale  = "—";
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStatusText))]
    private string _statusText = string.Empty;

    public bool HasStatusText => !string.IsNullOrEmpty(StatusText);
    [ObservableProperty] private bool   _isBusy;

    private bool _isInitialized;

    // --- Zero source ---

    [ObservableProperty]
    private ObservableCollection<ZeroSourceOption> _zeroSources = [];

    [ObservableProperty]
    private ZeroSourceOption? _selectedZeroSource;

    // --- Sub-ViewModels ---

    public UnitsViewModel    Units     { get; }
    public SetpointsViewModel Setpoints { get; }

    public CalibrationViewModel(IMixerScaleService service)
    {
        _service = service;
        Units     = new UnitsViewModel(service, this);
        Setpoints = new SetpointsViewModel(service, this);
    }

    public void Update(LiveWeightState? weight, DeviceSettingsState? settings)
    {
        _currentRawSum  = weight?.RawSum ?? 0;
        _currentSettings = settings;

        // Обновляем список источников для Zero
        var previousKey = SelectedZeroSource?.Key;
        var options = new List<ZeroSourceOption>();
        if (weight is not null)
        {
            options.Add(new ZeroSourceOption("raw",   "rawSum",   weight.RawSum,   weight.Valid));
            options.Add(new ZeroSourceOption("clean", "cleanSum", weight.CleanSum, weight.CleanValid));
            options.AddRange(weight.Ma.Select(ma =>
                new ZeroSourceOption($"ma:{ma.Name}", ma.Name, ma.RawSum, ma.Valid)));
        }
        ZeroSources.Clear();
        foreach (var o in options) ZeroSources.Add(o);

        SelectedZeroSource =
            options.FirstOrDefault(o => o.Key == previousKey) ??
            options.FirstOrDefault(o => o.Key == "ma:ma_3s") ??
            options.FirstOrDefault(o => o.Valid) ??
            options.FirstOrDefault();

        // Обновляем текущие значения с устройства и инициализируем ввод
        if (settings is not null)
        {
            CurrentOffset = settings.SumOffset.ToString("N0", CultureInfo.CurrentCulture);
            CurrentScale  = settings.SumScale.ToString("0.######", CultureInfo.InvariantCulture);

            if (!_isInitialized)
            {
                OffsetInput = CurrentOffset;
                ScaleInput  = CurrentScale;
                _isInitialized = true;
            }
        }
        else
        {
            CurrentOffset = "—";
            CurrentScale  = "—";
        }

        Units.Update(settings, SelectedZeroSource);
        Setpoints.Update(settings, SelectedZeroSource);
    }

    // При смене источника обновляем отображение текущего значения в строках
    partial void OnSelectedZeroSourceChanged(ZeroSourceOption? value)
    {
        Units.Update(_currentSettings, value);
        Setpoints.Update(_currentSettings, value);
    }

    [RelayCommand]
    private async Task ZeroAsync()
    {
        if (SelectedZeroSource is not { Valid: true } src) return;
        if (!TryReadForm(out var settings)) return;

        OffsetInput = src.RawSum.ToString(CultureInfo.InvariantCulture);
        await SaveAsync(settings with { SumOffset = src.RawSum }, $"Zero от {src.Name}: {src.RawSum}");
    }

    [RelayCommand]
    private async Task SaveCalibrationAsync()
    {
        if (!TryReadForm(out var settings)) return;
        await SaveAsync(settings, "Калибровка сохранена.");
    }

    internal async Task<bool> SaveAsync(DeviceSettingsState settings, string successMessage, Action<string>? setStatus = null)
    {
        IsBusy = true;
        setStatus ??= msg => StatusText = msg;
        setStatus("Сохранение…");
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
            var result = await _service.SaveSettingsAsync(settings, cts.Token);
            if (result is null)
            {
                setStatus("Ошибка сохранения.");
                return false;
            }

            setStatus(successMessage);
            _currentSettings = result;

            // Синхронизируем поля ввода с новыми сохраненными значениями
            OffsetInput = result.SumOffset.ToString(CultureInfo.InvariantCulture);
            ScaleInput  = result.SumScale.ToString("0.######", CultureInfo.InvariantCulture);

            return true;
        }
        finally
        {
            IsBusy = false;
        }
    }

    internal bool TryReadForm(out DeviceSettingsState settings)
    {
        settings = _currentSettings ?? new DeviceSettingsState();
        if (!TryParseLong(OffsetInput, out var offset))
        {
            StatusText = "Offset должен быть целым числом.";
            return false;
        }
        if (!TryParseDouble(ScaleInput, out var scale) || scale == 0)
        {
            StatusText = "Scale должен быть ненулевым числом.";
            return false;
        }
        settings = settings with { SumOffset = offset, SumScale = scale };
        return true;
    }

    // --- Парсеры ---

    internal static bool TryParseLong(string? text, out long value) =>
        long.TryParse(text, NumberStyles.Any, CultureInfo.InvariantCulture, out value) ||
        long.TryParse(text, NumberStyles.Any, CultureInfo.CurrentCulture, out value);

    internal static bool TryParseDouble(string? text, out double value)
    {
        text = (text ?? string.Empty).Trim().Replace(',', '.');
        return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }
}

/// <summary>Вариант источника для команды Zero.</summary>
internal sealed record ZeroSourceOption(string Key, string Name, long RawSum, bool Valid)
{
    public override string ToString() =>
        Valid ? $"{Name}: {RawSum}" : $"{Name}: нет данных";
}
