namespace MixerScale.Controller.Services;

/// <summary>
/// Реестр всех активных бетономешалок.
/// Создаёт сервисы, запускает их и хранит список.
/// </summary>
internal sealed class MixerRegistry : IMixerRegistry, IDisposable
{
    private readonly List<IMixerScaleService> _mixers = [];

    public IReadOnlyList<IMixerScaleService> Mixers => _mixers;
    public event Action? MixersChanged;

    public void AddRealMixer(string name, string endpoint, int pollIntervalMs = 1000, int timeoutMs = 5000)
    {
        var service = new RealScaleService(
            id: Guid.NewGuid().ToString("N"),
            displayName: name,
            endpoint: endpoint,
            pollIntervalMs: pollIntervalMs,
            timeoutMs: timeoutMs);
        service.Start();
        _mixers.Add(service);
        MixersChanged?.Invoke();
    }

    public void AddEmulatorMixer(string name)
    {
        var service = new EmulatorScaleService(
            id: Guid.NewGuid().ToString("N"),
            displayName: name);
        service.Start();
        _mixers.Add(service);
        MixersChanged?.Invoke();
    }

    public void RemoveMixer(string id)
    {
        var service = _mixers.FirstOrDefault(m => m.Id == id);
        if (service is null)
        {
            return;
        }

        service.Stop();
        _mixers.Remove(service);
        service.Dispose();
        MixersChanged?.Invoke();
    }

    public void Dispose()
    {
        foreach (var mixer in _mixers)
        {
            mixer.Stop();
            mixer.Dispose();
        }
        _mixers.Clear();
    }
}
