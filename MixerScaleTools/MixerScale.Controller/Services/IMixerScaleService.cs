using MixerScale.Controller.Models;

namespace MixerScale.Controller.Services;

/// <summary>
/// Абстракция одной бетономешалки. Не знает о том, реальная она или эмулируемая.
/// Реализация сама управляет таймером опроса и обновляет состояние.
/// </summary>
internal interface IMixerScaleService : IDisposable
{
    string Id { get; }
    string DisplayName { get; }

    // --- Последнее известное состояние (обновляется внутри сервиса) ---

    bool IsOnline { get; }
    LiveWeightState? LastWeight { get; }
    DeviceSettingsState? LastSettings { get; }
    WifiState? LastWifi { get; }
    UdpTelemetryState? LastUdp { get; }

    /// <summary>Статус последних API-вызовов для панели диагностики.</summary>
    IReadOnlyList<ApiCallStatus> LastCallStatuses { get; }

    /// <summary>Срабатывает в любом потоке при изменении состояния.</summary>
    event Action StateUpdated;

    void Start();
    void Stop();

    // --- Команды ---

    Task<DeviceSettingsState?> SaveSettingsAsync(DeviceSettingsState settings, CancellationToken ct);
}
