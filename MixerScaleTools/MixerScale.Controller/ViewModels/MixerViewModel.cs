using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Models;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// ViewModel одной бетономешалки. Слушает события сервиса и раздаёт данные дочерним VM.
/// Сам не знает, реальный сервис или эмулятор.
/// </summary>
internal sealed partial class MixerViewModel : ObservableObject, IDisposable
{
    public IMixerScaleService Service { get; }

    public string Header => Service.DisplayName;

    // Дочерние ViewModels для каждой секции интерфейса
    public MetricsViewModel Metrics { get; }
    public FiltersViewModel Filters { get; }
    public CalibrationViewModel Calibration { get; }
    public GraphViewModel Graph { get; }
    public ApiStatusViewModel ApiStatus { get; }
    public DashboardViewModel Dashboard { get; }

    /// <summary>True если сервис — эмулятор, т.е. реализует IEmulatorControl.</summary>
    public bool IsEmulator => Service is IEmulatorControl;

    public MixerViewModel(IMixerScaleService service)
    {
        Service = service;

        Metrics     = new MetricsViewModel();
        Filters     = new FiltersViewModel();
        Calibration = new CalibrationViewModel(service);
        Graph       = new GraphViewModel();
        ApiStatus   = new ApiStatusViewModel();
        Dashboard   = new DashboardViewModel();

        service.StateUpdated += OnStateUpdated;
    }

    /// <summary>
    /// Событие, на которое подписывается View для открытия окна дашборда.
    /// </summary>
    public event Action<DashboardViewModel>? DashboardFullscreenRequested;

    [RelayCommand]
    private void OpenFullscreenDashboard()
    {
        DashboardFullscreenRequested?.Invoke(Dashboard);
    }

    [RelayCommand]
    private void OpenEmulatorSettings()
    {
        if (Service is not IEmulatorControl emulator)
        {
            return;
        }

        // Открываем окно эмулятора; создание через фабрику, чтобы не зависеть от View
        var vm = new EmulatorViewModel(emulator, Service.DisplayName);
        EmulatorWindowRequested?.Invoke(vm);
    }

    /// <summary>
    /// Событие, на которое подписывается View для открытия окна эмулятора.
    /// Так ViewModel не знает ничего о классе EmulatorWindow.
    /// </summary>
    public event Action<EmulatorViewModel>? EmulatorWindowRequested;

    private void OnStateUpdated()
    {
        // StateUpdated приходит из потока таймера → переключаемся в UI-поток
        Dispatcher.UIThread.Post(ApplyState);
    }

    private void ApplyState()
    {
        var weight   = Service.LastWeight;
        var settings = Service.LastSettings;

        Metrics.Update(weight, Service.IsOnline);
        Filters.Update(weight, settings);
        Calibration.Update(weight, settings);
        Graph.Update(weight, settings);
        ApiStatus.Update(Service.LastCallStatuses, Service.LastWifi, Service.LastUdp);
        
        if (settings != null) Dashboard.UpdateSetpoints(settings.Setpoints);
        if (weight != null) Dashboard.UpdateLiveWeight(weight);
    }

    public void Dispose()
    {
        Service.StateUpdated -= OnStateUpdated;
    }
}
