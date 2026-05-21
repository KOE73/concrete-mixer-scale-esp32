using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using ScottPlot;
using ScottPlot.Avalonia;
using ScottPlot.Plottables;
using System.Net.Http;
using System.Text;
using System.Text.Json.Nodes;

namespace ScaleData.Analyzer;

internal sealed partial class MainWindow : Window
{
    private AvaPlot _plot = null!;
    private TextBlock _statusText = null!;
    private TextBlock _summaryText = null!;
    private TextBlock _lossText = null!;
    private TextBlock _folderText = null!;
    private ListBox _scaleFoldersList = null!;
    private ListBox _filesList = null!;
    private CheckBox _autoReloadCheckBox = null!;
    private CheckBox _autoScaleOnNewDataCheckBox = null!;
    private CheckBox _followNewDataCheckBox = null!;
    private CheckBox _showMaCheckBox = null!;
    private CheckBox _showRawCheckBox = null!;
    private CheckBox _showTotalCheckBox = null!;
    private CheckBox _showSensorsCheckBox = null!;
    private Button _collapseFilesButton = null!;
    private Button _resetZoomButton = null!;
    private Button _zeroSensorsButton = null!;
    private TextBlock _zeroSensorsStatusText = null!;
    private ColumnDefinition _filesColumn = null!;
    private Control _filesPanel = null!;
    private Crosshair? _crosshair;
    private System.Threading.Timer? _reloadTimer;
    private string _dataDirectory;
    private string _deviceBaseUrl;
    private string _selectedScaleDirectory;
    private string? _selectedFilePath;
    private string? _lastLoadedFilePath;
    private bool _filesCollapsed;
    private bool _refreshingFileBrowser;
    private TelemetryDataSet _dataSet = TelemetryDataSet.Empty;
    private long _lastFileLength = -1;
    private DateTime _lastFileWriteTime = DateTime.MinValue;
    private int _lastFileCount = -1;

    public MainWindow()
    {
        var settings = AnalyzerSettings.Load();
        _dataDirectory = settings.DataDirectory;
        _deviceBaseUrl = settings.DeviceBaseUrl.TrimEnd('/');
        Directory.CreateDirectory(_dataDirectory);
        _selectedScaleDirectory = Path.Combine(_dataDirectory, "1");
        Directory.CreateDirectory(_selectedScaleDirectory);

        Title = "Scale Data Analyzer";
        Width = 1280;
        Height = 820;
        MinWidth = 900;
        MinHeight = 640;
        InitializeComponent();
        FindControls();
        ConfigureLayoutEvents();
        ConfigurePlot();
        RefreshFileBrowser();
        ConfigureWatcher();
    }

    private void InitializeComponent()
    {
        AvaloniaXamlLoader.Load(this);
    }

    private void FindControls()
    {
        _plot = this.FindControl<AvaPlot>("Plot") ?? throw new InvalidOperationException("Plot control not found.");
        _statusText = this.FindControl<TextBlock>("StatusText") ?? throw new InvalidOperationException("StatusText control not found.");
        _summaryText = this.FindControl<TextBlock>("SummaryText") ?? throw new InvalidOperationException("SummaryText control not found.");
        _lossText = this.FindControl<TextBlock>("LossText") ?? throw new InvalidOperationException("LossText control not found.");
        _folderText = this.FindControl<TextBlock>("FolderText") ?? throw new InvalidOperationException("FolderText control not found.");
        _scaleFoldersList = this.FindControl<ListBox>("ScaleFoldersList") ?? throw new InvalidOperationException("ScaleFoldersList control not found.");
        _filesList = this.FindControl<ListBox>("FilesList") ?? throw new InvalidOperationException("FilesList control not found.");
        _autoReloadCheckBox = this.FindControl<CheckBox>("AutoReloadCheckBox") ?? throw new InvalidOperationException("AutoReloadCheckBox control not found.");
        _autoScaleOnNewDataCheckBox = this.FindControl<CheckBox>("AutoScaleOnNewDataCheckBox") ?? throw new InvalidOperationException("AutoScaleOnNewDataCheckBox control not found.");
        _followNewDataCheckBox = this.FindControl<CheckBox>("FollowNewDataCheckBox") ?? throw new InvalidOperationException("FollowNewDataCheckBox control not found.");
        _showMaCheckBox = this.FindControl<CheckBox>("ShowMaCheckBox") ?? throw new InvalidOperationException("ShowMaCheckBox control not found.");
        _showRawCheckBox = this.FindControl<CheckBox>("ShowRawCheckBox") ?? throw new InvalidOperationException("ShowRawCheckBox control not found.");
        _showTotalCheckBox = this.FindControl<CheckBox>("ShowTotalCheckBox") ?? throw new InvalidOperationException("ShowTotalCheckBox control not found.");
        _showSensorsCheckBox = this.FindControl<CheckBox>("ShowSensorsCheckBox") ?? throw new InvalidOperationException("ShowSensorsCheckBox control not found.");
        _collapseFilesButton = this.FindControl<Button>("CollapseFilesButton") ?? throw new InvalidOperationException("CollapseFilesButton control not found.");
        _resetZoomButton = this.FindControl<Button>("ResetZoomButton") ?? throw new InvalidOperationException("ResetZoomButton control not found.");
        _zeroSensorsButton = this.FindControl<Button>("ZeroSensorsButton") ?? throw new InvalidOperationException("ZeroSensorsButton control not found.");
        _zeroSensorsStatusText = this.FindControl<TextBlock>("ZeroSensorsStatusText") ?? throw new InvalidOperationException("ZeroSensorsStatusText control not found.");

        var rootGrid = this.FindControl<Grid>("RootGrid") ?? throw new InvalidOperationException("RootGrid control not found.");
        _filesColumn = rootGrid.ColumnDefinitions[0];
        _filesPanel = this.FindControl<Control>("FilesPanel") ?? throw new InvalidOperationException("FilesPanel control not found.");
    }

    private void ConfigureLayoutEvents()
    {
        _filesList.ItemTemplate = new FuncDataTemplate<FileBrowserItem>((item, _) => new TextBlock
        {
            Text = item?.DisplayText ?? string.Empty,
            FontFamily = Avalonia.Media.FontFamily.Parse("Consolas"),
            FontSize = 12,
            LineHeight = 13,
            Height = 16,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
            TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis
        });

        _autoReloadCheckBox.IsCheckedChanged += (_, _) => ConfigureWatcher();
        _autoScaleOnNewDataCheckBox.IsCheckedChanged += (_, _) =>
        {
            if (_autoScaleOnNewDataCheckBox.IsChecked == true)
            {
                RenderData(forceAutoScale: true);
            }
        };
        _showMaCheckBox.IsCheckedChanged += (_, _) => RenderData(forceAutoScale: false, preserveZoom: true);
        _showRawCheckBox.IsCheckedChanged += (_, _) => RenderData(forceAutoScale: false, preserveZoom: true);
        _showTotalCheckBox.IsCheckedChanged += (_, _) => RenderData(forceAutoScale: false, preserveZoom: true);
        _showSensorsCheckBox.IsCheckedChanged += (_, _) => RenderData(forceAutoScale: false, preserveZoom: true);

        _statusText.Text = "Файл не выбран";
        _scaleFoldersList.SelectionChanged += (_, _) =>
        {
            if (_refreshingFileBrowser)
            {
                return;
            }

            if (_scaleFoldersList.SelectedItem is ScaleFolderItem item)
            {
                _selectedScaleDirectory = item.Path;
                _selectedFilePath = null;
                RefreshFileBrowser();
                ConfigureWatcher();
            }
        };

        _filesList.SelectionChanged += (_, _) =>
        {
            if (_filesList.SelectedItem is FileBrowserItem item)
            {
                _selectedFilePath = item.Path;
                LoadFiles(new[] { item.Path }, updateBrowser: false);
            }
        };

        _collapseFilesButton.Click += (_, _) => ToggleFilesPanel();
        _resetZoomButton.Click += (_, _) => RenderData(forceAutoScale: true);
        _zeroSensorsButton.Click += async (_, _) => await ZeroSensorsAsync();
    }

    private async Task ZeroSensorsAsync()
    {
        _zeroSensorsButton.IsEnabled = false;
        _zeroSensorsStatusText.Text = "Читаю текущие raw...";

        try
        {
            using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
            JsonObject settings = await ReadJsonObjectAsync(client, $"{_deviceBaseUrl}/api/settings");
            JsonObject weight = await ReadJsonObjectAsync(client, $"{_deviceBaseUrl}/api/weight");

            var settingsChannels = settings["channels"]?.AsArray()
                ?? throw new InvalidOperationException("В /api/settings нет channels.");
            var weightChannels = weight["channels"]?.AsArray()
                ?? throw new InvalidOperationException("В /api/weight нет channels.");

            foreach (JsonNode? settingsChannel in settingsChannels)
            {
                if (settingsChannel is not JsonObject settingsObject ||
                    settingsObject["index"]?.GetValue<int>() is not int index)
                {
                    continue;
                }

                JsonObject? liveObject = weightChannels
                    .OfType<JsonObject>()
                    .FirstOrDefault(channel => channel["index"]?.GetValue<int>() == index);
                if (liveObject?["raw"] is JsonNode raw)
                {
                    settingsObject["offset"] = raw.GetValue<int>();
                }
            }

            using var content = new StringContent(settings.ToJsonString(), Encoding.UTF8, "application/json");
            using HttpResponseMessage response = await client.PostAsync($"{_deviceBaseUrl}/api/settings", content);
            response.EnsureSuccessStatusCode();

            _zeroSensorsStatusText.Text = "Offsets сохранены.";
        }
        catch (Exception ex)
        {
            _zeroSensorsStatusText.Text = ex.Message;
        }
        finally
        {
            _zeroSensorsButton.IsEnabled = true;
        }
    }

    private static async Task<JsonObject> ReadJsonObjectAsync(HttpClient client, string url)
    {
        string text = await client.GetStringAsync(url);
        return JsonNode.Parse(text)?.AsObject()
            ?? throw new InvalidOperationException($"Пустой JSON: {url}");
    }

    private void ToggleFilesPanel()
    {
        _filesCollapsed = !_filesCollapsed;
        if (_filesColumn is not null)
        {
            _filesColumn.Width = _filesCollapsed ? new GridLength(36) : new GridLength(360);
        }

        if (_filesPanel is not null)
        {
            _filesPanel.IsVisible = !_filesCollapsed;
        }

        _collapseFilesButton.Content = _filesCollapsed ? ">" : "<";
    }

    private void ConfigurePlot()
    {
        _plot.Plot.Title("Scale telemetry");
        _plot.Plot.XLabel("ESP32 ms");
        _plot.Plot.YLabel("value");
        _plot.PointerMoved += (_, eventArgs) =>
        {
            lock (_plot.Plot.Sync)
            {
                if (_crosshair is null || !_crosshair.IsVisible)
                {
                    return;
                }

                var position = eventArgs.GetPosition(_plot);
                var coordinates = _plot.Plot.GetCoordinates((float)position.X, (float)position.Y, _plot.Plot.Axes.Bottom, _plot.Plot.Axes.Left);
                _crosshair.Position = coordinates;
            }
            _plot.Refresh();
        };
        _plot.PointerExited += (_, _) =>
        {
            lock (_plot.Plot.Sync)
            {
                if (_crosshair is null)
                {
                    return;
                }

                _crosshair.IsVisible = false;
            }
            _plot.Refresh();
        };
        _plot.Refresh();
    }


    private void RefreshFileBrowser()
    {
        _refreshingFileBrowser = true;
        try
        {
            Directory.CreateDirectory(_dataDirectory);
            Directory.CreateDirectory(_selectedScaleDirectory);
            _folderText.Text = _dataDirectory;

            var scaleFolders = GetScaleFolders(_dataDirectory);
            _scaleFoldersList.ItemsSource = scaleFolders;
            var selectedScale = scaleFolders.FirstOrDefault(item => item.Path.Equals(_selectedScaleDirectory, StringComparison.OrdinalIgnoreCase))
                ?? scaleFolders.FirstOrDefault(item => item.Id == "1")
                ?? scaleFolders.FirstOrDefault();
            if (selectedScale is not null && !selectedScale.Path.Equals(_selectedScaleDirectory, StringComparison.OrdinalIgnoreCase))
            {
                _selectedScaleDirectory = selectedScale.Path;
            }
            _scaleFoldersList.SelectedItem = selectedScale;

            var items = GetCsvFiles(_selectedScaleDirectory)
                .Select(path =>
                {
                    var info = new FileInfo(path);
                    return new FileBrowserItem(path, info.Length, info.LastWriteTime);
                })
                .OrderByDescending(item => item.LastWriteTime)
                .ToArray();

            _lastFileCount = items.Length;
            _filesList.ItemsSource = items;
            if (_selectedFilePath is not null)
            {
                _filesList.SelectedItem = items.FirstOrDefault(item => item.Path.Equals(_selectedFilePath, StringComparison.OrdinalIgnoreCase));
            }

            _statusText.Text = items.Length == 0
                ? "CSV-файлы не найдены"
                : $"Весы {Path.GetFileName(_selectedScaleDirectory)}: {items.Length} CSV";
        }
        finally
        {
            _refreshingFileBrowser = false;
        }
    }

    private void LoadFiles(IReadOnlyCollection<string> files, bool updateBrowser = true, bool forceAutoScale = false)
    {
        if (updateBrowser)
        {
            RefreshFileBrowser();
        }

        _statusText.Text = files.Count == 0 ? "CSV-файлы не найдены" : $"Загружено файлов: {files.Count}";
        _dataSet = TelemetryCsvReader.Read(files);

        var currentFile = files.FirstOrDefault();
        bool isNewFile = currentFile != _lastLoadedFilePath;
        if (isNewFile)
        {
            _lastLoadedFilePath = currentFile;
        }

        if (currentFile is not null && File.Exists(currentFile))
        {
            try
            {
                var info = new FileInfo(currentFile);
                info.Refresh();
                _lastFileLength = info.Length;
                _lastFileWriteTime = info.LastWriteTime;
            }
            catch {}
        }

        RenderData(forceAutoScale || isNewFile);
    }

    private void ConfigureWatcher()
    {
        _reloadTimer?.Dispose();
        _reloadTimer = null;

        if (_autoReloadCheckBox.IsChecked != true || !Directory.Exists(_selectedScaleDirectory))
        {
            return;
        }

        _reloadTimer = new System.Threading.Timer(_ => Dispatcher.UIThread.Post(PollFileUpdates), null, 500, 500);
    }

    private void PollFileUpdates()
    {
        if (!Directory.Exists(_selectedScaleDirectory))
        {
            return;
        }

        try
        {
            var files = GetCsvFiles(_selectedScaleDirectory);
            if (files.Length != _lastFileCount)
            {
                _lastFileCount = files.Length;
                RefreshFileBrowser();
            }

            if (_selectedFilePath is not null && File.Exists(_selectedFilePath))
            {
                var info = new FileInfo(_selectedFilePath);
                info.Refresh();

                if (info.Length != _lastFileLength || info.LastWriteTime != _lastFileWriteTime)
                {
                    _lastFileLength = info.Length;
                    _lastFileWriteTime = info.LastWriteTime;
                    LoadFiles(new[] { _selectedFilePath }, updateBrowser: false);
                }
            }
        }
        catch
        {
            // Игнорируем временные ошибки доступа к файлам
        }
    }

    private static string[] GetCsvFiles(string folder)
    {
        if (!Directory.Exists(folder))
        {
            return Array.Empty<string>();
        }

        return Directory.GetFiles(folder, "*.csv").OrderBy(path => path, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    private static ScaleFolderItem[] GetScaleFolders(string folder)
    {
        Directory.CreateDirectory(Path.Combine(folder, "1"));
        return Directory.GetDirectories(folder)
            .Select(path => new ScaleFolderItem(Path.GetFileName(path), path))
            .OrderBy(item => uint.TryParse(item.Id, out var value) ? value : uint.MaxValue)
            .ThenBy(item => item.Id, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private void RenderData(bool forceAutoScale = true, bool preserveZoom = false)
    {
        lock (_plot.Plot.Sync)
        {
            // Сохраняем текущие границы перед очисткой, чтобы не терять зум
            // при переключении галочек видимости серий.
            AxisLimits savedLimits = _plot.Plot.Axes.GetLimits();
            _plot.Plot.Clear();

            if (_dataSet.Points.Count == 0)
            {
                _summaryText.Text = "Нет данных.";
                _lossText.Text = "-";
                _plot.Refresh();
                return;
            }

            var validPoints = _dataSet.Points.Where(point => !point.Flags.Contains("invalid", StringComparison.OrdinalIgnoreCase)).ToArray();
            if (validPoints.Length == 0)
            {
                _summaryText.Text = "Нет валидных данных.";
                _lossText.Text = "-";
                _plot.Refresh();
                return;
            }

            var x = validPoints.Select(point => (double)point.EspMilliseconds).ToArray();
            var rawSum = validPoints.Select(point => (double)point.RawSum).ToArray();
            var kgSum = validPoints.Select(point => point.KilogramsSum).ToArray();

            if (_showRawCheckBox.IsChecked == true)
            {
                var rawPlot = _plot.Plot.Add.Scatter(x, rawSum);
                rawPlot.LegendText = "raw_sum";
            }

            if (_showTotalCheckBox.IsChecked == true)
            {
                var kgPlot = _plot.Plot.Add.Scatter(x, kgSum);
                kgPlot.LegendText = "kg_sum";
                kgPlot.Axes.YAxis = _plot.Plot.Axes.Right;
            }

            // Отображаем каждую из имеющихся скользящих средних
            if (_showMaCheckBox.IsChecked == true)
            {
                var windowSizes = validPoints
                    .SelectMany(p => p.MovingAverages.Select(ma => ma.WindowSizeSec))
                    .Distinct()
                    .OrderBy(w => w)
                    .ToArray();

                foreach (var winSize in windowSizes)
                {
                    var maX = new List<double>();
                    var maY = new List<double>();
                    foreach (var point in validPoints)
                    {
                        var ma = point.MovingAverages.FirstOrDefault(m => m.WindowSizeSec == winSize);
                        if (ma != default)
                        {
                            maX.Add(point.EspMilliseconds);
                            maY.Add(ma.ValueKg);
                        }
                    }
                    if (maX.Count > 0)
                    {
                        var maPlot = _plot.Plot.Add.Scatter(maX.ToArray(), maY.ToArray());
                        maPlot.LegendText = $"MA {winSize}s";
                        maPlot.Axes.YAxis = _plot.Plot.Axes.Right;
                    }
                }
            }

            // Отображаем данные каждого датчика тонкой линией без маркеров
            if (_showSensorsCheckBox.IsChecked == true && validPoints[0].RawValues.Length > 0)
            {
                int sensorCount = validPoints[0].RawValues.Length;
                for (int s = 0; s < sensorCount; s++)
                {
                    var sensorY = validPoints.Select(point => (double)point.RawValues[s]).ToArray();
                    var sensorPlot = _plot.Plot.Add.Scatter(x, sensorY);
                    sensorPlot.LegendText = $"sensor_{s + 1}";
                    sensorPlot.LineWidth = 1.0f;
                    sensorPlot.MarkerSize = 2.0f;
                }
            }

            _plot.Plot.Axes.Right.Label.Text = "kg_sum";
            _plot.Plot.ShowLegend(Alignment.UpperLeft);
            _crosshair = _plot.Plot.Add.Crosshair(x[0], rawSum[0]);
            _crosshair.IsVisible = true;
            _crosshair.LineWidth = 1;

            if (preserveZoom)
            {
                // Галочки видимости серий (MA/RAW/Total/Датчики) — зум не трогаем никогда
                _plot.Plot.Axes.SetLimits(savedLimits.Left, savedLimits.Right, savedLimits.Bottom, savedLimits.Top);
            }
            else if (forceAutoScale)
            {
                // Явный сброс зума: новый файл, кнопка "Сбросить зум"
                _plot.Plot.Axes.AutoScale();
            }
            else if (_autoScaleOnNewDataCheckBox.IsChecked == true)
            {
                // Новые данные при включённом Автозуме
                _plot.Plot.Axes.AutoScale();
            }
            else if (_followNewDataCheckBox.IsChecked == true)
            {
                // Режим слежения: сдвигаем вид вправо вслед за данными
                var xLast = x[^1];
                var limits = savedLimits;
                var width = limits.Right - limits.Left;
                try
                {
                    var logPath = Path.Combine(_dataDirectory, "follow_debug.log");
                    var debugLine = $"{DateTime.Now:HH:mm:ss.fff} | xLast={xLast:F1} | Left={limits.Left:F1} | Right={limits.Right:F1} | Width={width:F1} | ShouldShift={xLast > limits.Right}";
                    if (width > 0 && xLast > limits.Right)
                    {
                        var newRight = xLast + width * 0.05;
                        var newLeft = newRight - width;
                        _plot.Plot.Axes.SetLimits(newLeft, newRight, limits.Bottom, limits.Top);
                        debugLine += $" | Shifted to {newLeft:F1}..{newRight:F1}";
                    }
                    else
                    {
                        _plot.Plot.Axes.SetLimits(limits.Left, limits.Right, limits.Bottom, limits.Top);
                    }
                    File.AppendAllText(logPath, debugLine + Environment.NewLine);
                }
                catch {}
            }
            else
            {
                // Нет автозума, нет слежения — сохраняем зум пользователя
                _plot.Plot.Axes.SetLimits(savedLimits.Left, savedLimits.Right, savedLimits.Bottom, savedLimits.Top);
            }
        }

        _plot.Refresh();

        var validPointsForSummary = _dataSet.Points.Where(point => !point.Flags.Contains("invalid", StringComparison.OrdinalIgnoreCase)).ToArray();
        _summaryText.Text =
            $"Строк: {_dataSet.Points.Count} (валидных: {validPointsForSummary.Length})\n" +
            $"seq: {_dataSet.Points.First().Sequence} - {_dataSet.Points.Last().Sequence}\n" +
            $"ms: {_dataSet.Points.First().EspMilliseconds} - {_dataSet.Points.Last().EspMilliseconds}\n" +
            $"raw каналов: {_dataSet.Points.Max(point => point.RawValues.Length)}\n" +
            $"kg min/max: {(validPointsForSummary.Length > 0 ? $"{validPointsForSummary.Min(point => point.KilogramsSum):0.###} / {validPointsForSummary.Max(point => point.KilogramsSum):0.###}" : "- / -")}";

        _lossText.Text = _dataSet.LostPackets.Count == 0
            ? "Пропусков не найдено."
            : string.Join(Environment.NewLine, _dataSet.LostPackets.Take(5).Select(loss => $"{loss.AfterSequence} -> {loss.BeforeSequence}: -{loss.MissingCount}"));
    }
}
