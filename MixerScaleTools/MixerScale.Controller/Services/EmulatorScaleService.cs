using MixerScale.Controller.Models;

namespace MixerScale.Controller.Services;

/// <summary>
/// Эмулятор бетономешалки. Реализует IMixerScaleService так же, как и реальный сервис.
/// Дополнительно реализует IEmulatorControl — для окна настройки эмулятора.
/// Ползунок веса в окне эмулятора задаёт RawSum, сервис генерирует фиктивный LiveWeightState.
/// </summary>
internal sealed class EmulatorScaleService : IMixerScaleService, IEmulatorControl
{
    // Имена MA-фильтров, которые будет эмулировать сервис
    private static readonly string[] MaNames = ["ma_3s", "ma_10s", "ma_30s"];

    private readonly System.Timers.Timer _timer;
    private long _rawSum;
    private ulong _sequence;

    // Настройки хранятся в памяти; при SaveSettingsAsync обновляются и сразу возвращаются
    private DeviceSettingsState _settings = new()
    {
        SumOffset = 0,
        SumScale = 1.0,
        Units =
        [
            new UnitConversionState { Name = "kg", RawPerUnit = 12700.0 }
        ],
        Setpoints =
        [
            new SetpointState { Name = "X1", RawValue = 100000 },
            new SetpointState { Name = "X2", RawValue = 1000000 },
            new SetpointState { Name = "X3", RawValue = 2000000 }
        ]
    };

    public string Id { get; }
    public string DisplayName { get; }
    public bool IsOnline => true;

    public LiveWeightState? LastWeight { get; private set; }
    public DeviceSettingsState? LastSettings => _settings;

    public WifiState? LastWifi { get; } = new()
    {
        Ap = new AccessPointState { Started = true, Ssid = "Emulator-AP" },
        Sta = new StationState { Connected = false }
    };

    public UdpTelemetryState? LastUdp { get; } = new()
    {
        Enabled = false,
        ScaleId = 0,
        TargetHost = "emulator",
        Port = 0
    };

    public IReadOnlyList<ApiCallStatus> LastCallStatuses { get; } =
    [
        new("GET /api/state.cbor",   true, TimeSpan.Zero),
        new("GET /api/wifi",         true, TimeSpan.Zero),
        new("GET /api/settings",     true, TimeSpan.Zero),
        new("GET /api/udp-telemetry",true, TimeSpan.Zero),
    ];

    public event Action? StateUpdated;

    // --- IEmulatorControl ---

    public long RawSum
    {
        get => _rawSum;
        set
        {
            _rawSum = Math.Clamp(value, RawMin, RawMax);
            PushState();
        }
    }

    public long RawMin => -500_000;
    public long RawMax =>  5_000_000;

    public EmulatorScaleService(string id, string displayName)
    {
        Id = id;
        DisplayName = displayName;

        // Периодически «тикаем», чтобы Delta и Sequence обновлялись даже без движения ползунка
        _timer = new System.Timers.Timer(1000) { AutoReset = true };
        _timer.Elapsed += (_, _) => PushState();
    }

    public void Start()
    {
        PushState();
        _timer.Start();
    }

    public void Stop() => _timer.Stop();

    public Task<DeviceSettingsState?> SaveSettingsAsync(DeviceSettingsState settings, CancellationToken ct)
    {
        // Эмулятор сохраняет настройки в памяти мгновенно
        _settings = settings;
        StateUpdated?.Invoke();
        return Task.FromResult<DeviceSettingsState?>(_settings);
    }

    public void Dispose() => _timer.Dispose();

    private void PushState()
    {
        _sequence++;
        var raw = _rawSum;

        // Вес в кг по текущим настройкам калибровки
        var weight = _settings.SumScale != 0
            ? (raw - _settings.SumOffset) * _settings.SumScale
            : 0.0;

        // Все MA-фильтры равны текущему значению — простая симуляция без сглаживания
        LastWeight = new LiveWeightState
        {
            Sequence = _sequence,
            Valid = true,
            CleanValid = true,
            RawSum = raw,
            CleanSum = raw,
            Weight = weight,
            Ma = MaNames.Select(name => new MaState
            {
                Name = name,
                Valid = true,
                RawSum = raw,
                Weight = weight
            }).ToArray()
        };

        StateUpdated?.Invoke();
    }
}
