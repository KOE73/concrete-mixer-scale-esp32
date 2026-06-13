namespace MixerScale.Controller.Services;

/// <summary>
/// Реестр всех активных бетономешалок.
/// Каждая может быть реальной (HTTP) или эмулируемой — компоненту, который их отображает, это не важно.
/// </summary>
internal interface IMixerRegistry
{
    IReadOnlyList<IMixerScaleService> Mixers { get; }

    /// <summary>Срабатывает при добавлении или удалении бетономешалки.</summary>
    event Action MixersChanged;

    void AddRealMixer(string name, string endpoint, int pollIntervalMs = 1000, int timeoutMs = 5000);
    void AddEmulatorMixer(string name);
    void RemoveMixer(string id);
}
