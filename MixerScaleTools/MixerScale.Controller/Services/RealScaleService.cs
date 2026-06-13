using MixerScale.Controller.Api;
using MixerScale.Controller.Configuration;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.Services;

/// <summary>
/// Реальная реализация сервиса бетономешалки — работает через HTTP с ESP32.
/// Самостоятельно опрашивает устройство по таймеру и хранит последнее состояние.
/// </summary>
internal sealed class RealScaleService : IMixerScaleService
{
    private readonly MixerScaleApiClient _client;
    private readonly int _timeoutMs;
    private readonly System.Timers.Timer _timer;
    private readonly List<ApiCallStatus> _statuses = [];
    private bool _polling;
    private bool _statusLoaded;

    public string Id { get; }
    public string DisplayName { get; }
    public bool IsOnline { get; private set; }
    public LiveWeightState? LastWeight { get; private set; }
    public DeviceSettingsState? LastSettings { get; private set; }
    public WifiState? LastWifi { get; private set; }
    public UdpTelemetryState? LastUdp { get; private set; }
    public IReadOnlyList<ApiCallStatus> LastCallStatuses => _statuses;

    public event Action? StateUpdated;

    public RealScaleService(string id, string displayName, string endpoint, int pollIntervalMs, int timeoutMs)
    {
        Id = id;
        DisplayName = displayName;
        _timeoutMs = timeoutMs;

        _client = new MixerScaleApiClient(endpoint);

        _timer = new System.Timers.Timer(Math.Max(500, pollIntervalMs)) { AutoReset = true };
        _timer.Elapsed += async (_, _) => await PollAsync();
    }

    public void Start()
    {
        _timer.Start();
        // Первый опрос сразу, не ждём интервал
        _ = PollAsync();
    }

    public void Stop() => _timer.Stop();

    public async Task<DeviceSettingsState?> SaveSettingsAsync(DeviceSettingsState settings, CancellationToken ct)
    {
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        cts.CancelAfter(TimeSpan.FromMilliseconds(_timeoutMs * 6));

        var result = await _client.SaveSettingsAsync(settings, cts.Token);
        SetStatus("POST /api/settings", result.Success, result.Elapsed, result.Error);

        if (result.Success && result.Value is not null)
        {
            LastSettings = result.Value;
            StateUpdated?.Invoke();
            return result.Value;
        }

        StateUpdated?.Invoke();
        return null;
    }

    public void Dispose()
    {
        _timer.Dispose();
        _client.Dispose();
    }

    private async Task PollAsync()
    {
        if (_polling)
        {
            return;
        }

        _polling = true;
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(_timeoutMs));
            var weightResult = await _client.GetStateAsync(cts.Token);
            IsOnline = weightResult.Success;
            LastWeight = weightResult.Success ? weightResult.Value : null;
            SetStatus("GET /api/state.cbor", weightResult.Success, weightResult.Elapsed, weightResult.Error);

            // Wifi, settings, UDP загружаем один раз при первом успешном соединении
            if (weightResult.Success && !_statusLoaded)
            {
                using var wifiCts = new CancellationTokenSource(TimeSpan.FromMilliseconds(_timeoutMs));
                var wifiResult = await _client.GetWifiAsync(wifiCts.Token);
                LastWifi = wifiResult.Value;
                SetStatus("GET /api/wifi", wifiResult.Success, wifiResult.Elapsed, wifiResult.Error);

                using var settingsCts = new CancellationTokenSource(TimeSpan.FromMilliseconds(_timeoutMs));
                var settingsResult = await _client.GetSettingsAsync(settingsCts.Token);
                LastSettings = settingsResult.Value;
                SetStatus("GET /api/settings", settingsResult.Success, settingsResult.Elapsed, settingsResult.Error);

                using var udpCts = new CancellationTokenSource(TimeSpan.FromMilliseconds(_timeoutMs));
                var udpResult = await _client.GetUdpTelemetryAsync(udpCts.Token);
                LastUdp = udpResult.Value;
                SetStatus("GET /api/udp-telemetry", udpResult.Success, udpResult.Elapsed, udpResult.Error);

                if (wifiResult.Success && settingsResult.Success && udpResult.Success)
                {
                    _statusLoaded = true;
                }
            }

            StateUpdated?.Invoke();
        }
        finally
        {
            _polling = false;
        }
    }

    private void SetStatus(string name, bool success, TimeSpan elapsed, string error)
    {
        var idx = _statuses.FindIndex(s => s.Name == name);
        var status = new ApiCallStatus(name, success, elapsed, error);
        if (idx >= 0)
        {
            _statuses[idx] = status;
        }
        else
        {
            _statuses.Add(status);
        }
    }
}
