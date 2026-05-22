using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Markup.Xaml;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia;
using System.Globalization;
using System.Text;
using MixerScale.Controller.Api;
using MixerScale.Controller.Configuration;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.Views;

internal sealed partial class MainWindow : Window
{
    private readonly ControllerSettings _settings;
    private readonly MixerScaleApiClient _apiClient;
    private readonly DispatcherTimer _pollTimer;
    private readonly IBrush _onlineBrush = new SolidColorBrush(Color.Parse("#22A06B"));
    private readonly IBrush _offlineBrush = new SolidColorBrush(Color.Parse("#C9372C"));
    private readonly IBrush _unknownBrush = new SolidColorBrush(Color.Parse("#697783"));
    private readonly HashSet<string> _selectedMaNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "ma_3s"
    };
    private readonly HashSet<string> _selectedUnitKeys = new(StringComparer.OrdinalIgnoreCase)
    {
        BaseUnitKey
    };
    private readonly List<GraphSample> _graphHistory = [];
    private static readonly string[] GraphColors =
    [
        "#1264A3", "#E34935", "#2E7D32", "#8E44AD",
        "#D97706", "#00897B", "#5E6AD2", "#C2185B"
    ];

    private const string BaseUnitKey = "base";
    private TextBlock _endpointText = null!;
    private Border _connectionDot = null!;
    private TextBlock _connectionText = null!;
    private Button _refreshButton = null!;
    private TextBlock _sequenceText = null!;
    private TextBlock _rawSumText = null!;
    private TextBlock _cleanSumText = null!;
    private TextBlock _primaryMaText = null!;
    private TextBlock _sampleStateText = null!;
    private ContentControl _filtersTableHost = null!;
    private TextBlock _wifiText = null!;
    private TextBlock _udpText = null!;
    private TextBlock _settingsText = null!;
    private TextBox _sumOffsetInput = null!;
    private TextBox _sumScaleInput = null!;
    private ComboBox _zeroSourceCombo = null!;
    private Button _zeroButton = null!;
    private Button _saveCalibrationButton = null!;
    private ItemsControl _unitsList = null!;
    private TextBox _unitNameInput = null!;
    private TextBox _unitRawInput = null!;
    private Button _addUnitButton = null!;
    private TextBlock _calibrationStatusText = null!;
    private ItemsControl _setpointsList = null!;
    private TextBox _setpointNameInput = null!;
    private TextBox _setpointRawInput = null!;
    private Button _useCurrentSetpointButton = null!;
    private Button _addSetpointButton = null!;
    private ContentControl _graphHost = null!;
    private TextBox _graphDurationInput = null!;
    private TextBlock _graphLegendText = null!;
    private GraphView _graphView = null!;
    private TextBlock _apiStatusText = null!;
    private DeviceSettingsState? _deviceSettings;
    private LiveWeightState? _liveState;
    private ulong? _lastGraphSequence;
    private bool _refreshing;
    private bool _settingsEditorActive;

    public MainWindow()
    {
        _settings = ControllerSettings.Load();
        _apiClient = new MixerScaleApiClient(_settings);
        InitializeComponent();
        FindControls();

        _endpointText.Text = _settings.DeviceBaseUrl;
        _refreshButton.Click += async (_, _) => await RefreshAsync();
        _zeroButton.Click += async (_, _) => await ZeroAsync();
        _saveCalibrationButton.Click += async (_, _) => await SaveCalibrationAsync();
        _addUnitButton.Click += async (_, _) => await AddUnitAsync();
        _zeroSourceCombo.SelectionChanged += (_, _) =>
        {
            RenderUnits();
            RenderSetpoints();
        };
        _useCurrentSetpointButton.Click += (_, _) => FillSetpointFromCurrent();
        _addSetpointButton.Click += async (_, _) => await AddSetpointAsync();
        _graphDurationInput.LostFocus += (_, _) => TrimGraphHistory();

        _pollTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(Math.Max(250, _settings.PollIntervalMs))
        };
        _pollTimer.Tick += async (_, _) => await RefreshAsync();
        _pollTimer.Start();

        _ = RefreshAsync();
    }

    protected override void OnClosed(EventArgs e)
    {
        _pollTimer.Stop();
        _apiClient.Dispose();
        base.OnClosed(e);
    }

    private void InitializeComponent()
    {
        AvaloniaXamlLoader.Load(this);
    }

    private void FindControls()
    {
        _endpointText = this.FindControl<TextBlock>("EndpointText") ?? throw Missing(nameof(_endpointText));
        _connectionDot = this.FindControl<Border>("ConnectionDot") ?? throw Missing(nameof(_connectionDot));
        _connectionText = this.FindControl<TextBlock>("ConnectionText") ?? throw Missing(nameof(_connectionText));
        _refreshButton = this.FindControl<Button>("RefreshButton") ?? throw Missing(nameof(_refreshButton));
        _sequenceText = this.FindControl<TextBlock>("SequenceText") ?? throw Missing(nameof(_sequenceText));
        _rawSumText = this.FindControl<TextBlock>("RawSumText") ?? throw Missing(nameof(_rawSumText));
        _cleanSumText = this.FindControl<TextBlock>("CleanSumText") ?? throw Missing(nameof(_cleanSumText));
        _primaryMaText = this.FindControl<TextBlock>("PrimaryMaText") ?? throw Missing(nameof(_primaryMaText));
        _sampleStateText = this.FindControl<TextBlock>("SampleStateText") ?? throw Missing(nameof(_sampleStateText));
        _filtersTableHost = this.FindControl<ContentControl>("FiltersTableHost") ?? throw Missing(nameof(_filtersTableHost));
        _wifiText = this.FindControl<TextBlock>("WifiText") ?? throw Missing(nameof(_wifiText));
        _udpText = this.FindControl<TextBlock>("UdpText") ?? throw Missing(nameof(_udpText));
        _settingsText = this.FindControl<TextBlock>("SettingsText") ?? throw Missing(nameof(_settingsText));
        _sumOffsetInput = this.FindControl<TextBox>("SumOffsetInput") ?? throw Missing(nameof(_sumOffsetInput));
        _sumScaleInput = this.FindControl<TextBox>("SumScaleInput") ?? throw Missing(nameof(_sumScaleInput));
        _zeroSourceCombo = this.FindControl<ComboBox>("ZeroSourceCombo") ?? throw Missing(nameof(_zeroSourceCombo));
        _zeroButton = this.FindControl<Button>("ZeroButton") ?? throw Missing(nameof(_zeroButton));
        _saveCalibrationButton = this.FindControl<Button>("SaveCalibrationButton") ?? throw Missing(nameof(_saveCalibrationButton));
        _unitsList = this.FindControl<ItemsControl>("UnitsList") ?? throw Missing(nameof(_unitsList));
        _unitNameInput = this.FindControl<TextBox>("UnitNameInput") ?? throw Missing(nameof(_unitNameInput));
        _unitRawInput = this.FindControl<TextBox>("UnitRawInput") ?? throw Missing(nameof(_unitRawInput));
        _addUnitButton = this.FindControl<Button>("AddUnitButton") ?? throw Missing(nameof(_addUnitButton));
        _calibrationStatusText = this.FindControl<TextBlock>("CalibrationStatusText") ?? throw Missing(nameof(_calibrationStatusText));
        _setpointsList = this.FindControl<ItemsControl>("SetpointsList") ?? throw Missing(nameof(_setpointsList));
        _setpointNameInput = this.FindControl<TextBox>("SetpointNameInput") ?? throw Missing(nameof(_setpointNameInput));
        _setpointRawInput = this.FindControl<TextBox>("SetpointRawInput") ?? throw Missing(nameof(_setpointRawInput));
        _useCurrentSetpointButton = this.FindControl<Button>("UseCurrentSetpointButton") ?? throw Missing(nameof(_useCurrentSetpointButton));
        _addSetpointButton = this.FindControl<Button>("AddSetpointButton") ?? throw Missing(nameof(_addSetpointButton));
        _graphHost = this.FindControl<ContentControl>("GraphHost") ?? throw Missing(nameof(_graphHost));
        _graphDurationInput = this.FindControl<TextBox>("GraphDurationInput") ?? throw Missing(nameof(_graphDurationInput));
        _graphLegendText = this.FindControl<TextBlock>("GraphLegendText") ?? throw Missing(nameof(_graphLegendText));
        _graphView = new GraphView();
        _graphHost.Content = _graphView;
        _apiStatusText = this.FindControl<TextBlock>("ApiStatusText") ?? throw Missing(nameof(_apiStatusText));
    }

    private static InvalidOperationException Missing(string name) => new($"Control not found: {name}");

    private async Task RefreshAsync()
    {
        if (_refreshing)
        {
            return;
        }

        _refreshing = true;
        _refreshButton.IsEnabled = false;
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(_settings.RequestTimeoutMs * 6));

            var weight = await _apiClient.GetStateAsync(cts.Token);
            RenderWeight(weight);
            RenderConnection(weight.Success, weight.Error);

            var wifi = await _apiClient.GetWifiAsync(cts.Token);
            RenderWifi(wifi);

            var deviceSettings = await _apiClient.GetSettingsAsync(cts.Token);
            RenderSettings(deviceSettings);

            var udp = await _apiClient.GetUdpTelemetryAsync(cts.Token);
            RenderUdp(udp);

            RenderApiStatus(weight, wifi, deviceSettings, udp);
        }
        finally
        {
            _refreshButton.IsEnabled = true;
            _refreshing = false;
        }
    }

    private void RenderConnection(bool online, string error)
    {
        _connectionDot.Background = online ? _onlineBrush : _offlineBrush;
        _connectionText.Text = online ? "связь есть" : $"нет связи: {Short(error)}";
    }

    private void RenderWeight(ApiCallResult<LiveWeightState> result)
    {
        if (!result.Success || result.Value is null)
        {
            _liveState = null;
            _sequenceText.Text = "-";
            _rawSumText.Text = "-";
            _cleanSumText.Text = "-";
            _primaryMaText.Text = "-";
            _sampleStateText.Text = "offline";
            _filtersTableHost.Content = null;
            RenderZeroSources();
            if (!IsCalibrationInputFocused())
            {
                RenderUnits();
                RenderSetpoints();
            }
            RenderGraph();
            return;
        }

        var state = result.Value;
        _liveState = state;
        CaptureGraphSample();
        var primaryMa = state.Ma.FirstOrDefault(filter => filter.Name == "ma_3s")
            ?? state.Ma.FirstOrDefault(filter => filter.Name.StartsWith("ma_", StringComparison.OrdinalIgnoreCase));

        _sequenceText.Text = state.Sequence.ToString();
        _rawSumText.Text = state.RawSum.ToString();
        _cleanSumText.Text = state.CleanSum.ToString();
        _primaryMaText.Text = primaryMa is null || !primaryMa.Valid ? "-" : primaryMa.RawSum.ToString();
        _sampleStateText.Text = state.Valid
            ? (state.CleanValid ? "valid" : $"reject: {state.RejectReason}")
            : $"invalid: {state.RejectReason}";

        RenderMaMatrix();
        RenderZeroSources();
        if (!IsCalibrationInputFocused())
        {
            RenderUnits();
            RenderSetpoints();
        }
        RenderGraph();
    }

    private void RenderWifi(ApiCallResult<WifiState> result)
    {
        if (!result.Success || result.Value is null)
        {
            _wifiText.Text = Short(result.Error);
            return;
        }

        var wifi = result.Value;
        _wifiText.Text =
            $"AP: {(wifi.Ap?.Started == true ? wifi.Ap.Ssid : "-")}  {wifi.Ap?.Mac}\n" +
            $"STA: {(wifi.Sta?.Connected == true ? wifi.Sta.Ssid : "нет")}  {wifi.Sta?.Ip}\n" +
            $"MAC: {wifi.Sta?.Mac}";
    }

    private void RenderSettings(ApiCallResult<DeviceSettingsState> result, bool forceFormUpdate = false)
    {
        if (!result.Success || result.Value is null)
        {
            _settingsText.Text = Short(result.Error);
            return;
        }

        _deviceSettings = result.Value;
        _settingsText.Text =
            $"sumOffset: {result.Value.SumOffset}\n" +
            $"sumScale: {result.Value.SumScale:0.######}";
        if (forceFormUpdate || !IsCalibrationInputFocused())
        {
            _sumOffsetInput.Text = result.Value.SumOffset.ToString(CultureInfo.InvariantCulture);
            _sumScaleInput.Text = result.Value.SumScale.ToString("0.######", CultureInfo.InvariantCulture);
            RenderUnits();
            RenderSetpoints();
            RenderMaMatrix();
            RenderGraph();
        }
    }

    private void RenderUdp(ApiCallResult<UdpTelemetryState> result)
    {
        if (!result.Success || result.Value is null)
        {
            _udpText.Text = Short(result.Error);
            return;
        }

        var udp = result.Value;
        _udpText.Text =
            $"scale_id: {udp.ScaleId}\n" +
            $"state: {(udp.Enabled ? "enabled" : "disabled")}\n" +
            $"target: {udp.TargetHost}:{udp.Port}";
    }

    private void RenderApiStatus(
        ApiCallResult<LiveWeightState> weight,
        ApiCallResult<WifiState> wifi,
        ApiCallResult<DeviceSettingsState> settings,
        ApiCallResult<UdpTelemetryState> udp)
    {
        _apiStatusText.Text =
            $"GET /api/state.cbor      {Status(weight)}\n" +
            $"GET /api/wifi            {Status(wifi)}\n" +
            $"GET /api/settings        {Status(settings)}\n" +
            $"GET /api/udp-telemetry   {Status(udp)}";
    }

    private void RenderZeroSources()
    {
        var selectedKey = (_zeroSourceCombo.SelectedItem as ZeroSourceOption)?.Key;
        var options = new List<ZeroSourceOption>();
        if (_liveState is not null)
        {
            options.Add(new ZeroSourceOption("raw", "rawSum", _liveState.RawSum, _liveState.Valid));
            options.Add(new ZeroSourceOption("clean", "cleanSum", _liveState.CleanSum, _liveState.CleanValid));
            options.AddRange(_liveState.Ma.Select(ma =>
                new ZeroSourceOption($"ma:{ma.Name}", ma.Name, ma.RawSum, ma.Valid)));
        }

        _zeroSourceCombo.ItemsSource = options;
        _zeroSourceCombo.SelectedItem =
            options.FirstOrDefault(option => option.Key == selectedKey)
            ?? options.FirstOrDefault(option => option.Key == "ma:ma_3s")
            ?? options.FirstOrDefault(option => option.Valid)
            ?? options.FirstOrDefault();
        _zeroButton.IsEnabled = _zeroSourceCombo.SelectedItem is ZeroSourceOption { Valid: true };
    }

    private void RenderMaMatrix()
    {
        if (_liveState is null)
        {
            _filtersTableHost.Content = null;
            return;
        }
        if (!_liveState.Ma.Any(filter => _selectedMaNames.Contains(filter.Name)))
        {
            var primary = _liveState.Ma.FirstOrDefault(filter => filter.Name == "ma_3s")
                ?? _liveState.Ma.FirstOrDefault(filter => filter.Valid)
                ?? _liveState.Ma.FirstOrDefault();
            if (primary is not null)
            {
                _selectedMaNames.Add(primary.Name);
            }
        }

        var columns = GetUnitColumns().ToArray();
        _filtersTableHost.Content = CreateMaTable(_liveState.Ma, columns);
    }

    private Control CreateMaTable(IReadOnlyList<MaState> filters, IReadOnlyList<UnitColumn> columns)
    {
        var table = new Grid
        {
            ColumnDefinitions = CreateMaColumnDefinitions(columns.Count),
            RowDefinitions = CreateMaRowDefinitions(filters.Count + 1),
            Margin = new Avalonia.Thickness(0)
        };

        AddTableCell(table, "raw", 0, 2, bold: true, alignRight: true);
        for (var i = 0; i < columns.Count; ++i)
        {
            var column = columns[i];
            var checkBox = new CheckBox
            {
                Content = column.Name,
                IsChecked = _selectedUnitKeys.Contains(column.Key),
                Height = 24,
                MinHeight = 0,
                Margin = new Avalonia.Thickness(8, 0, 0, 0),
                VerticalAlignment = VerticalAlignment.Center
            };
            checkBox.Checked += (_, _) => { SetSelected(_selectedUnitKeys, column.Key, true); RenderGraph(); };
            checkBox.Unchecked += (_, _) => { SetSelected(_selectedUnitKeys, column.Key, false); RenderGraph(); };
            Grid.SetColumn(checkBox, 3 + i);
            table.Children.Add(checkBox);
        }

        for (var rowIndex = 0; rowIndex < filters.Count; ++rowIndex)
        {
            var filter = filters[rowIndex];
            var gridRow = rowIndex + 1;
            var sourceCheckBox = new CheckBox
            {
                IsChecked = _selectedMaNames.Contains(filter.Name),
                IsEnabled = filter.Valid,
                Height = 24,
                MinHeight = 0,
                Margin = new Avalonia.Thickness(0),
                VerticalAlignment = VerticalAlignment.Center
            };
            sourceCheckBox.Checked += (_, _) => { SetSelected(_selectedMaNames, filter.Name, true); RenderGraph(); };
            sourceCheckBox.Unchecked += (_, _) => { SetSelected(_selectedMaNames, filter.Name, false); RenderGraph(); };
            Grid.SetRow(sourceCheckBox, gridRow);
            table.Children.Add(sourceCheckBox);

            AddTableCell(table, filter.Name, gridRow, 1);
            AddTableCell(table,
                         filter.Valid ? filter.RawSum.ToString(CultureInfo.InvariantCulture) : "-",
                         gridRow,
                         2,
                         alignRight: true);
            for (var i = 0; i < columns.Count; ++i)
            {
                AddTableCell(table, FormatUnitValue(filter, columns[i]), gridRow, 3 + i, alignRight: true);
            }
        }

        return table;
    }

    private static ColumnDefinitions CreateMaColumnDefinitions(int unitColumnCount)
    {
        var definitions = new StringBuilder("Auto,*,Auto");
        for (var i = 0; i < unitColumnCount; ++i)
        {
            definitions.Append(",Auto");
        }
        return new ColumnDefinitions(definitions.ToString());
    }

    private static RowDefinitions CreateMaRowDefinitions(int rowCount)
    {
        var definitions = new StringBuilder("Auto");
        for (var i = 1; i < rowCount; ++i)
        {
            definitions.Append(",Auto");
        }
        return new RowDefinitions(definitions.ToString());
    }

    private static void AddTableCell(
        Grid table,
        string text,
        int row,
        int column,
        bool bold = false,
        bool alignRight = false)
    {
        var block = new TextBlock
        {
            Text = text,
            FontFamily = FontFamily.Parse("Consolas"),
            FontWeight = bold ? FontWeight.SemiBold : FontWeight.Normal,
            VerticalAlignment = VerticalAlignment.Center,
            TextAlignment = alignRight ? TextAlignment.Right : TextAlignment.Left,
            Margin = new Avalonia.Thickness(column == 0 ? 0 : 10, 0, 0, 0)
        };
        Grid.SetRow(block, row);
        Grid.SetColumn(block, column);
        table.Children.Add(block);
    }

    private IReadOnlyList<UnitColumn> GetUnitColumns()
    {
        var columns = new List<UnitColumn>
        {
            new(BaseUnitKey, "kg", null)
        };
        if (_deviceSettings is null)
        {
            return columns;
        }

        for (var i = 0; i < _deviceSettings.Units.Count; ++i)
        {
            var unit = _deviceSettings.Units[i];
            if (unit.RawPerUnit > 0 && !string.IsNullOrWhiteSpace(unit.Name))
            {
                columns.Add(new UnitColumn($"unit:{i}:{unit.Name}", unit.Name, unit));
            }
        }
        return columns;
    }

    private string FormatUnitValue(MaState filter, UnitColumn column)
    {
        if (!filter.Valid || _deviceSettings is null)
        {
            return "-";
        }
        if (column.Unit is null)
        {
            return filter.Weight.ToString("0", CultureInfo.InvariantCulture);
        }

        var value = (filter.RawSum - _deviceSettings.SumOffset) / column.Unit.RawPerUnit;
        return $"{value:0.###} {column.Unit.Name}";
    }

    private double? UnitValue(MaState filter, UnitColumn column)
    {
        if (!filter.Valid || _deviceSettings is null)
        {
            return null;
        }
        return column.Unit is null
            ? filter.Weight
            : (filter.RawSum - _deviceSettings.SumOffset) / column.Unit.RawPerUnit;
    }

    private static void SetSelected(HashSet<string> set, string key, bool selected)
    {
        if (selected) {
            set.Add(key);
        } else {
            set.Remove(key);
        }
    }

    private void RenderUnits()
    {
        if (_deviceSettings is null)
        {
            _unitsList.ItemsSource = Array.Empty<Control>();
            return;
        }

        var units = _deviceSettings.Units.ToArray();
        if (units.Length == 0)
        {
            _unitsList.ItemsSource = new Control[]
            {
                new TextBlock
                {
                    Text = "нет условных единиц",
                    Foreground = Brushes.Gray
                }
            };
            return;
        }

        _unitsList.ItemsSource = units
            .Select((unit, index) => CreateUnitRow(unit, index))
            .ToArray();
    }

    private void RenderSetpoints()
    {
        if (_deviceSettings is null)
        {
            _setpointsList.ItemsSource = Array.Empty<Control>();
            return;
        }

        var setpoints = _deviceSettings.Setpoints.ToArray();
        if (setpoints.Length == 0)
        {
            _setpointsList.ItemsSource = new Control[]
            {
                new TextBlock
                {
                    Text = "нет уставок",
                    Foreground = Brushes.Gray
                }
            };
            return;
        }

        _setpointsList.ItemsSource = setpoints
            .Select((setpoint, index) => CreateSetpointRow(setpoint, index))
            .ToArray();
    }

    private async Task ZeroAsync()
    {
        if (_zeroSourceCombo.SelectedItem is not ZeroSourceOption { Valid: true } source)
        {
            _calibrationStatusText.Text = "Нет валидного источника для Zero.";
            return;
        }
        if (!TryReadCalibrationForm(out var settings))
        {
            return;
        }

        _sumOffsetInput.Text = source.RawSum.ToString(CultureInfo.InvariantCulture);
        await SaveSettingsAndRenderAsync(settings with { SumOffset = source.RawSum }, $"Zero от {source.Name}: {source.RawSum}");
    }

    private async Task SaveCalibrationAsync()
    {
        if (!TryReadCalibrationForm(out var settings))
        {
            return;
        }

        await SaveSettingsAndRenderAsync(settings, "Калибровка сохранена.");
    }

    private async Task AddUnitAsync()
    {
        if (!TryReadCalibrationForm(out var settings))
        {
            return;
        }

        var name = (_unitNameInput.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            _calibrationStatusText.Text = "Введите единицу измерения.";
            return;
        }
        if (name.Length > 5)
        {
            name = name[..5];
        }
        if (!IsEnglishLettersOnly(name))
        {
            _calibrationStatusText.Text = "Единица измерения: только английские буквы.";
            return;
        }
        if (!TryParseDouble(_unitRawInput.Text, out var rawPerUnit) || rawPerUnit <= 0)
        {
            _calibrationStatusText.Text = "raw/ед. должен быть положительным числом.";
            return;
        }

        var units = settings.Units.ToList();
        if (units.Count >= 8)
        {
            _calibrationStatusText.Text = "Максимум 8 условных единиц.";
            return;
        }

        units.Add(new UnitConversionState
        {
            Name = name,
            RawPerUnit = rawPerUnit
        });

        _unitNameInput.Text = string.Empty;
        _unitRawInput.Text = string.Empty;
        await SaveSettingsAndRenderAsync(settings with { Units = units }, $"Добавлена единица {name}.");
    }

    private async Task UpdateUnitAsync(int index, TextBox nameInput, TextBox rawInput)
    {
        if (_deviceSettings is null || index < 0 || index >= _deviceSettings.Units.Count)
        {
            return;
        }

        var name = (nameInput.Text ?? string.Empty).Trim();
        if (name.Length > 5)
        {
            name = name[..5];
        }
        if (!IsEnglishLettersOnly(name))
        {
            _calibrationStatusText.Text = "Единица измерения: только английские буквы.";
            return;
        }
        if (!TryParseDouble(rawInput.Text, out var rawPerUnit) || rawPerUnit <= 0)
        {
            _calibrationStatusText.Text = "raw/ед. должен быть положительным числом.";
            return;
        }

        var units = _deviceSettings.Units.ToList();
        units[index] = units[index] with
        {
            Name = name,
            RawPerUnit = rawPerUnit
        };
        await SaveSettingsAndRenderAsync(_deviceSettings with { Units = units }, $"Единица {name} сохранена.");
    }

    private async Task DeleteUnitAsync(int index)
    {
        if (_deviceSettings is null || index < 0 || index >= _deviceSettings.Units.Count)
        {
            return;
        }

        var units = _deviceSettings.Units.ToList();
        var removed = units[index].Name;
        units.RemoveAt(index);
        await SaveSettingsAndRenderAsync(_deviceSettings with { Units = units }, $"Удалена единица {removed}.");
    }

    private void FillSetpointFromCurrent()
    {
        if (_zeroSourceCombo.SelectedItem is ZeroSourceOption { Valid: true } source)
        {
            _setpointRawInput.Text = source.RawSum.ToString(CultureInfo.InvariantCulture);
        }
    }

    private async Task AddSetpointAsync()
    {
        if (!TryReadCalibrationForm(out var settings))
        {
            return;
        }

        var name = (_setpointNameInput.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            _calibrationStatusText.Text = "Введите имя уставки.";
            return;
        }
        if (!TryParseLong(_setpointRawInput.Text, out var rawValue))
        {
            _calibrationStatusText.Text = "raw уставки должен быть целым числом.";
            return;
        }

        var setpoints = settings.Setpoints.ToList();
        if (setpoints.Count >= 16)
        {
            _calibrationStatusText.Text = "Максимум 16 уставок.";
            return;
        }

        setpoints.Add(new SetpointState
        {
            Name = name.Length > 24 ? name[..24] : name,
            RawValue = rawValue
        });
        _setpointNameInput.Text = string.Empty;
        _setpointRawInput.Text = string.Empty;
        await SaveSettingsAndRenderAsync(settings with { Setpoints = setpoints }, $"Добавлена уставка {name}.");
    }

    private async Task UpdateSetpointAsync(int index, TextBox nameInput, TextBox rawInput)
    {
        if (_deviceSettings is null || index < 0 || index >= _deviceSettings.Setpoints.Count)
        {
            return;
        }

        var name = (nameInput.Text ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            _calibrationStatusText.Text = "Введите имя уставки.";
            return;
        }
        if (!TryParseLong(rawInput.Text, out var rawValue))
        {
            _calibrationStatusText.Text = "raw уставки должен быть целым числом.";
            return;
        }

        var setpoints = _deviceSettings.Setpoints.ToList();
        setpoints[index] = setpoints[index] with
        {
            Name = name.Length > 24 ? name[..24] : name,
            RawValue = rawValue
        };
        await SaveSettingsAndRenderAsync(_deviceSettings with { Setpoints = setpoints }, $"Уставка {name} сохранена.");
    }

    private async Task DeleteSetpointAsync(int index)
    {
        if (_deviceSettings is null || index < 0 || index >= _deviceSettings.Setpoints.Count)
        {
            return;
        }

        var setpoints = _deviceSettings.Setpoints.ToList();
        var removed = setpoints[index].Name;
        setpoints.RemoveAt(index);
        await SaveSettingsAndRenderAsync(_deviceSettings with { Setpoints = setpoints }, $"Удалена уставка {removed}.");
    }

    private async Task SaveSettingsAndRenderAsync(DeviceSettingsState settings, string successMessage)
    {
        _zeroButton.IsEnabled = false;
        _saveCalibrationButton.IsEnabled = false;
        _addUnitButton.IsEnabled = false;
        _addSetpointButton.IsEnabled = false;
        _useCurrentSetpointButton.IsEnabled = false;
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(_settings.RequestTimeoutMs * 6));
            var result = await _apiClient.SaveSettingsAsync(settings, cts.Token);
            if (!result.Success || result.Value is null)
            {
                _calibrationStatusText.Text = Short(result.Error);
                return;
            }

            RenderSettings(result, forceFormUpdate: true);
            _calibrationStatusText.Text = successMessage;
        }
        finally
        {
            _zeroButton.IsEnabled = _zeroSourceCombo.SelectedItem is ZeroSourceOption { Valid: true };
            _saveCalibrationButton.IsEnabled = true;
            _addUnitButton.IsEnabled = true;
            _addSetpointButton.IsEnabled = true;
            _useCurrentSetpointButton.IsEnabled = true;
        }
    }

    private bool TryReadCalibrationForm(out DeviceSettingsState settings)
    {
        settings = _deviceSettings ?? new DeviceSettingsState();
        if (!TryParseLong(_sumOffsetInput.Text, out var sumOffset))
        {
            _calibrationStatusText.Text = "sumOffset должен быть целым числом.";
            return false;
        }
        if (!TryParseDouble(_sumScaleInput.Text, out var sumScale) || sumScale == 0)
        {
            _calibrationStatusText.Text = "sumScale должен быть ненулевым числом.";
            return false;
        }

        settings = settings with
        {
            SumOffset = sumOffset,
            SumScale = sumScale
        };
        return true;
    }

    private void CaptureGraphSample()
    {
        if (_liveState is null || _deviceSettings is null)
        {
            return;
        }
        if (_lastGraphSequence == _liveState.Sequence)
        {
            return;
        }
        _lastGraphSequence = _liveState.Sequence;

        var columns = GetUnitColumns();
        var values = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        foreach (var filter in _liveState.Ma)
        {
            foreach (var column in columns)
            {
                var value = UnitValue(filter, column);
                if (value is not null && double.IsFinite(value.Value))
                {
                    values[SeriesKey(filter.Name, column.Key)] = value.Value;
                }
            }
        }

        _graphHistory.Add(new GraphSample(DateTimeOffset.UtcNow, values));
        TrimGraphHistory();
        RenderGraph();
    }

    private void TrimGraphHistory()
    {
        var duration = TimeSpan.FromSeconds(GetGraphDurationSeconds());
        var minTime = DateTimeOffset.UtcNow - duration;
        _graphHistory.RemoveAll(sample => sample.Time < minTime);
        RenderGraph();
    }

    private int GetGraphDurationSeconds()
    {
        if (!int.TryParse(_graphDurationInput.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var seconds))
        {
            seconds = 60;
        }
        return Math.Clamp(seconds, 5, 600);
    }

    private void RenderGraph()
    {
        var columns = GetUnitColumns()
            .Where(column => _selectedUnitKeys.Contains(column.Key))
            .ToArray();
        var series = new List<GraphSeries>();
        var markers = new List<GraphMarker>();
        var colorIndex = 0;

        if (_liveState is not null)
        {
            foreach (var filter in _liveState.Ma.Where(filter => _selectedMaNames.Contains(filter.Name)))
            {
                foreach (var column in columns)
                {
                    var key = SeriesKey(filter.Name, column.Key);
                    var points = _graphHistory
                        .Where(sample => sample.Values.ContainsKey(key))
                        .Select(sample => new GraphPoint(sample.Time, sample.Values[key]))
                        .ToArray();
                    if (points.Length == 0)
                    {
                        continue;
                    }

                    series.Add(new GraphSeries(
                        $"{filter.Name}/{column.Name}",
                        Color.Parse(GraphColors[colorIndex++ % GraphColors.Length]),
                        points));
                }
            }
        }

        if (_deviceSettings is not null)
        {
            foreach (var setpoint in _deviceSettings.Setpoints.Where(setpoint => !string.IsNullOrWhiteSpace(setpoint.Name)))
            {
                foreach (var column in columns)
                {
                    markers.Add(new GraphMarker(
                        $"{setpoint.Name}/{column.Name}",
                        SetpointValue(setpoint, column)));
                }
            }
        }

        _graphView.Duration = TimeSpan.FromSeconds(GetGraphDurationSeconds());
        _graphView.Series = series;
        _graphView.Markers = markers;
        _graphView.InvalidateVisual();
        _graphLegendText.Text = string.Join("   ", series.Select(item => item.Name));
    }

    private static string SeriesKey(string maName, string unitKey) => $"{maName}|{unitKey}";

    private double SetpointValue(SetpointState setpoint, UnitColumn column) =>
        column.Unit is null
            ? (setpoint.RawValue - (_deviceSettings?.SumOffset ?? 0)) * (_deviceSettings?.SumScale ?? 1.0)
            : (setpoint.RawValue - (_deviceSettings?.SumOffset ?? 0)) / column.Unit.RawPerUnit;

    private static string Status<T>(ApiCallResult<T> result) =>
        result.Success ? $"OK {result.Elapsed.TotalMilliseconds:0} ms" : $"ERR {Short(result.Error)}";

    private Control CreateUnitRow(UnitConversionState unit, int index)
    {
        var row = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("64,*,Auto,Auto,Auto"),
            Margin = new Avalonia.Thickness(0, 2, 0, 2)
        };
        var nameInput = new TextBox
        {
            Text = unit.Name,
            MaxLength = 5,
            FontFamily = FontFamily.Parse("Consolas"),
            Margin = new Avalonia.Thickness(0, 0, 8, 0)
        };
        TrackEditorFocus(nameInput);
        row.Children.Add(nameInput);

        var rawInput = new TextBox
        {
            Text = unit.RawPerUnit.ToString("0.###", CultureInfo.InvariantCulture),
            FontFamily = FontFamily.Parse("Consolas"),
            Margin = new Avalonia.Thickness(0, 0, 8, 0)
        };
        TrackEditorFocus(rawInput);
        Grid.SetColumn(rawInput, 1);
        row.Children.Add(rawInput);

        row.Children.Add(Cell(CurrentUnitText(unit), 2));

        var save = new Button
        {
            Content = "Сохранить",
            Padding = new Avalonia.Thickness(8, 3),
            Margin = new Avalonia.Thickness(8, 0, 0, 0)
        };
        save.Click += async (_, _) => await UpdateUnitAsync(index, nameInput, rawInput);
        Grid.SetColumn(save, 3);
        row.Children.Add(save);

        var delete = new Button
        {
            Content = "Удалить",
            Padding = new Avalonia.Thickness(8, 3),
            Margin = new Avalonia.Thickness(8, 0, 0, 0)
        };
        delete.Click += async (_, _) => await DeleteUnitAsync(index);
        Grid.SetColumn(delete, 4);
        row.Children.Add(delete);
        return row;
    }

    private string CurrentUnitText(UnitConversionState unit)
    {
        var source = _zeroSourceCombo.SelectedItem as ZeroSourceOption;
        if (_deviceSettings is not null && source is { Valid: true } && unit.RawPerUnit > 0)
        {
            var value = (source.RawSum - _deviceSettings.SumOffset) / unit.RawPerUnit;
            return $"{value:0.###} {unit.Name}";
        }
        return "-";
    }

    private Control CreateSetpointRow(SetpointState setpoint, int index)
    {
        var row = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("*,*,Auto,Auto,Auto,Auto"),
            Margin = new Avalonia.Thickness(0, 2, 0, 2)
        };
        var nameInput = new TextBox
        {
            Text = setpoint.Name,
            MaxLength = 24,
            Margin = new Avalonia.Thickness(0, 0, 8, 0)
        };
        TrackEditorFocus(nameInput);
        row.Children.Add(nameInput);

        var rawInput = new TextBox
        {
            Text = setpoint.RawValue.ToString(CultureInfo.InvariantCulture),
            FontFamily = FontFamily.Parse("Consolas"),
            Margin = new Avalonia.Thickness(0, 0, 8, 0)
        };
        TrackEditorFocus(rawInput);
        Grid.SetColumn(rawInput, 1);
        row.Children.Add(rawInput);

        row.Children.Add(Cell(SetpointDeltaText(setpoint), 2));

        var current = new Button
        {
            Content = "Текущее",
            Padding = new Avalonia.Thickness(8, 3),
            Margin = new Avalonia.Thickness(8, 0, 0, 0)
        };
        current.Click += (_, _) => FillRawFromCurrent(rawInput);
        Grid.SetColumn(current, 3);
        row.Children.Add(current);

        var save = new Button
        {
            Content = "Сохранить",
            Padding = new Avalonia.Thickness(8, 3),
            Margin = new Avalonia.Thickness(8, 0, 0, 0)
        };
        save.Click += async (_, _) => await UpdateSetpointAsync(index, nameInput, rawInput);
        Grid.SetColumn(save, 4);
        row.Children.Add(save);

        var delete = new Button
        {
            Content = "Удалить",
            Padding = new Avalonia.Thickness(8, 3),
            Margin = new Avalonia.Thickness(8, 0, 0, 0)
        };
        delete.Click += async (_, _) => await DeleteSetpointAsync(index);
        Grid.SetColumn(delete, 5);
        row.Children.Add(delete);
        return row;
    }

    private void FillRawFromCurrent(TextBox rawInput)
    {
        if (_zeroSourceCombo.SelectedItem is ZeroSourceOption { Valid: true } source)
        {
            rawInput.Text = source.RawSum.ToString(CultureInfo.InvariantCulture);
        }
    }

    private string SetpointDeltaText(SetpointState setpoint)
    {
        if (_zeroSourceCombo.SelectedItem is ZeroSourceOption { Valid: true } source)
        {
            var delta = source.RawSum - setpoint.RawValue;
            return delta.ToString(CultureInfo.InvariantCulture);
        }
        return "-";
    }

    private static TextBlock Cell(string text, int column = 0)
    {
        var block = new TextBlock
        {
            Text = text,
            FontFamily = FontFamily.Parse("Consolas"),
            VerticalAlignment = VerticalAlignment.Center,
            Margin = column == 0 ? default : new Avalonia.Thickness(16, 0, 0, 0)
        };
        Grid.SetColumn(block, column);
        return block;
    }

    private bool IsCalibrationInputFocused() =>
        _settingsEditorActive ||
        _sumOffsetInput.IsFocused ||
        _sumScaleInput.IsFocused ||
        _unitNameInput.IsFocused ||
        _unitRawInput.IsFocused ||
        _setpointNameInput.IsFocused ||
        _setpointRawInput.IsFocused;

    private void TrackEditorFocus(Control control)
    {
        control.GotFocus += (_, _) => _settingsEditorActive = true;
        control.LostFocus += (_, _) => _settingsEditorActive = false;
    }

    private static bool TryParseLong(string? text, out long value) =>
        long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ||
        long.TryParse(text, NumberStyles.Integer, CultureInfo.CurrentCulture, out value);

    private static bool TryParseDouble(string? text, out double value)
    {
        text = (text ?? string.Empty).Trim().Replace(',', '.');
        return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    private static bool IsEnglishLettersOnly(string value) =>
        !string.IsNullOrWhiteSpace(value) &&
        value.All(ch => (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'));

    private static string Short(string error) =>
        string.IsNullOrWhiteSpace(error) ? "-" : error.Length <= 80 ? error : error[..77] + "...";

    private sealed record ZeroSourceOption(string Key, string Name, long RawSum, bool Valid)
    {
        public override string ToString() => Valid ? $"{Name}: {RawSum}" : $"{Name}: нет данных";
    }

    private sealed record UnitColumn(string Key, string Name, UnitConversionState? Unit);

    private sealed record GraphSample(DateTimeOffset Time, IReadOnlyDictionary<string, double> Values);

    private sealed record GraphPoint(DateTimeOffset Time, double Value);

    private sealed record GraphSeries(string Name, Color Color, IReadOnlyList<GraphPoint> Points);

    private sealed record GraphMarker(string Name, double Value);

    private sealed class GraphView : Control
    {
        public IReadOnlyList<GraphSeries> Series { get; set; } = [];

        public IReadOnlyList<GraphMarker> Markers { get; set; } = [];

        public TimeSpan Duration { get; set; } = TimeSpan.FromSeconds(60);

        public override void Render(DrawingContext context)
        {
            base.Render(context);
            var bounds = Bounds;
            context.DrawRectangle(Brushes.White, new Pen(Brushes.LightGray, 1), bounds);

            var allPoints = Series.SelectMany(series => series.Points).ToArray();
            if (allPoints.Length == 0 && Markers.Count == 0)
            {
                return;
            }

            var now = DateTimeOffset.UtcNow;
            var start = now - Duration;
            var values = allPoints.Select(point => point.Value)
                .Concat(Markers.Select(marker => marker.Value))
                .ToArray();
            var min = values.Min();
            var max = values.Max();
            if (Math.Abs(max - min) < 0.000001)
            {
                max += 1;
                min -= 1;
            }

            var plot = new Rect(bounds.X + 8, bounds.Y + 8, Math.Max(1, bounds.Width - 16), Math.Max(1, bounds.Height - 16));
            for (var i = 1; i < 4; ++i)
            {
                var y = plot.Top + plot.Height * i / 4.0;
                context.DrawLine(new Pen(Brushes.Gainsboro, 1), new Point(plot.Left, y), new Point(plot.Right, y));
            }

            foreach (var marker in Markers)
            {
                var yRatio = (marker.Value - min) / (max - min);
                var y = plot.Bottom - yRatio * plot.Height;
                var pen = new Pen(Brushes.IndianRed, 1);
                context.DrawLine(pen, new Point(plot.Left, y), new Point(plot.Right, y));
                context.DrawText(
                    new FormattedText(
                        $"{marker.Name} {marker.Value:0.###}",
                        CultureInfo.CurrentCulture,
                        FlowDirection.LeftToRight,
                        Typeface.Default,
                        12,
                        Brushes.IndianRed),
                    new Point(plot.Left + 6, Math.Max(plot.Top, y - 16)));
            }

            foreach (var series in Series)
            {
                var brush = new SolidColorBrush(series.Color);
                var pen = new Pen(brush, 2);
                Point? previous = null;
                foreach (var point in series.Points.Where(point => point.Time >= start))
                {
                    var xRatio = (point.Time - start).TotalMilliseconds / Math.Max(1, Duration.TotalMilliseconds);
                    var yRatio = (point.Value - min) / (max - min);
                    var current = new Point(
                        plot.Left + xRatio * plot.Width,
                        plot.Bottom - yRatio * plot.Height);
                    if (previous is not null)
                    {
                        context.DrawLine(pen, previous.Value, current);
                    }
                    previous = current;
                }
            }
        }
    }
}
