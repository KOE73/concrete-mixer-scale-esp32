using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Данные для отрисовки графика. Хранит историю семплов и собирает серии линий.
/// Получает данные через Update() от MixerViewModel.
/// </summary>
internal sealed partial class GraphViewModel : ObservableObject
{
    private static readonly string[] GraphColors =
    [
        "#1264A3", "#E34935", "#2E7D32", "#8E44AD",
        "#D97706", "#00897B", "#5E6AD2", "#C2185B"
    ];

    private readonly List<GraphSample> _history = [];
    private ulong? _lastSequence;

    // Наборы выбранных для отображения MA-фильтров и единиц
    private readonly HashSet<string> _selectedMaNames   = new(StringComparer.OrdinalIgnoreCase) { "ma_3s" };
    private readonly HashSet<string> _selectedUnitKeys  = new(StringComparer.OrdinalIgnoreCase) { "base" };

    [ObservableProperty] private int _durationSeconds = 60;
    [ObservableProperty] private IReadOnlyList<GraphSeries>  _series  = [];
    [ObservableProperty] private IReadOnlyList<GraphMarker>  _markers = [];
    [ObservableProperty] private string _legendText = string.Empty;

    // Колонки единиц для выбора (строятся из DeviceSettings)
    [ObservableProperty] private IReadOnlyList<UnitColumn> _unitColumns = [];

    public void Update(LiveWeightState? weight, DeviceSettingsState? settings)
    {
        if (weight is null || settings is null)
        {
            Series  = [];
            Markers = [];
            LegendText = string.Empty;
            return;
        }

        // Строим список доступных единиц
        var cols = BuildUnitColumns(settings);
        UnitColumns = cols;

        // Добавляем новый семпл (по sequence, чтобы не дублировать)
        if (_lastSequence != weight.Sequence)
        {
            _lastSequence = weight.Sequence;
            AddSample(weight, settings, cols);
            TrimHistory();
        }

        RebuildSeries(weight, settings, cols);
    }

    public void SetMaSelected(string name, bool selected)
    {
        if (selected) _selectedMaNames.Add(name);
        else          _selectedMaNames.Remove(name);
    }

    public void SetUnitSelected(string key, bool selected)
    {
        if (selected) _selectedUnitKeys.Add(key);
        else          _selectedUnitKeys.Remove(key);
    }

    public bool IsMaSelected(string name)   => _selectedMaNames.Contains(name);
    public bool IsUnitSelected(string key)  => _selectedUnitKeys.Contains(key);

    private void AddSample(LiveWeightState weight, DeviceSettingsState settings, IReadOnlyList<UnitColumn> cols)
    {
        var values = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        foreach (var ma in weight.Ma)
        {
            foreach (var col in cols)
            {
                var v = CalcUnitValue(ma, col, settings);
                if (v is not null && double.IsFinite(v.Value))
                {
                    values[SeriesKey(ma.Name, col.Key)] = v.Value;
                }
            }
        }
        _history.Add(new GraphSample(DateTimeOffset.UtcNow, values));
    }

    private void TrimHistory()
    {
        var minTime = DateTimeOffset.UtcNow - TimeSpan.FromSeconds(Math.Max(5, DurationSeconds));
        _history.RemoveAll(s => s.Time < minTime);
    }

    private void RebuildSeries(LiveWeightState weight, DeviceSettingsState settings, IReadOnlyList<UnitColumn> cols)
    {
        var selectedCols = cols.Where(c => _selectedUnitKeys.Contains(c.Key)).ToArray();
        var series  = new List<GraphSeries>();
        var markers = new List<GraphMarker>();
        var ci      = 0;

        foreach (var ma in weight.Ma.Where(m => _selectedMaNames.Contains(m.Name)))
        {
            foreach (var col in selectedCols)
            {
                var key    = SeriesKey(ma.Name, col.Key);
                var points = _history
                    .Where(s => s.Values.ContainsKey(key))
                    .Select(s => new GraphPoint(s.Time, s.Values[key]))
                    .ToArray();
                if (points.Length == 0) continue;

                series.Add(new GraphSeries(
                    $"{ma.Name}/{col.Name}",
                    Color.Parse(GraphColors[ci++ % GraphColors.Length]),
                    points));
            }
        }

        foreach (var sp in settings.Setpoints.Where(s => !string.IsNullOrWhiteSpace(s.Name)))
        {
            foreach (var col in selectedCols)
            {
                var v = CalcSetpointValue(sp, col, settings);
                markers.Add(new GraphMarker($"{sp.Name}/{col.Name}", v));
            }
        }

        Series     = series;
        Markers    = markers;
        LegendText = string.Join("   ", series.Select(s => s.Name));
    }

    private static IReadOnlyList<UnitColumn> BuildUnitColumns(DeviceSettingsState settings)
    {
        var cols = new List<UnitColumn> { new("base", "kg", null) };
        for (var i = 0; i < settings.Units.Count; i++)
        {
            var u = settings.Units[i];
            if (u.RawPerUnit > 0 && !string.IsNullOrWhiteSpace(u.Name))
            {
                cols.Add(new UnitColumn($"unit:{i}:{u.Name}", u.Name, u));
            }
        }
        return cols;
    }

    private static double? CalcUnitValue(MaState ma, UnitColumn col, DeviceSettingsState settings)
    {
        if (!ma.Valid) return null;
        return col.Unit is null
            ? ma.Weight
            : (ma.RawSum - settings.SumOffset) / col.Unit.RawPerUnit;
    }

    private static double CalcSetpointValue(SetpointState sp, UnitColumn col, DeviceSettingsState settings) =>
        col.Unit is null
            ? (sp.RawValue - settings.SumOffset) * settings.SumScale
            : (sp.RawValue - settings.SumOffset) / col.Unit.RawPerUnit;

    private static string SeriesKey(string maName, string unitKey) => $"{maName}|{unitKey}";
}

// --- Вспомогательные типы данных для графика ---

internal sealed record UnitColumn(string Key, string Name, MixerScale.Controller.Models.UnitConversionState? Unit);
internal sealed record GraphSample(DateTimeOffset Time, IReadOnlyDictionary<string, double> Values);
internal sealed record GraphPoint(DateTimeOffset Time, double Value);
internal sealed record GraphSeries(string Name, Color Color, IReadOnlyList<GraphPoint> Points);
internal sealed record GraphMarker(string Name, double Value);
