using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Корневая ViewModel главного окна.
/// Управляет списком бетономешалок и делегирует добавление/удаление реестру.
/// </summary>
internal sealed partial class MainViewModel : ObservableObject
{
    private readonly IMixerRegistry _registry;

    [ObservableProperty]
    private ObservableCollection<object> _tabs = [];

    [ObservableProperty]
    private object? _selectedTab;

    private readonly SettingsViewModel _settingsTab;

    public MainViewModel(IMixerRegistry registry)
    {
        _registry = registry;
        _settingsTab = new SettingsViewModel(this);
        _registry.MixersChanged += SyncWithRegistry;
        SyncWithRegistry();
    }

    [RelayCommand]
    private void AddEmulator()
    {
        var num = Tabs.OfType<MixerViewModel>().Count() + 1;
        _registry.AddEmulatorMixer($"Эмулятор {num}");
    }

    [RelayCommand]
    private void AddReal(string endpoint)
    {
        var name = $"Бетономешалка {Tabs.OfType<MixerViewModel>().Count() + 1}";
        _registry.AddRealMixer(name, endpoint);
    }

    [RelayCommand]
    private void RemoveMixer(MixerViewModel? mixer)
    {
        if (mixer is null)
        {
            return;
        }

        _registry.RemoveMixer(mixer.Service.Id);
    }

    private void SyncWithRegistry()
    {
        var mixers = Tabs.OfType<MixerViewModel>().ToList();

        // Удаляем VM для бетономешалок, которых больше нет в реестре
        var toRemove = mixers
            .Where(vm => !_registry.Mixers.Any(s => s.Id == vm.Service.Id))
            .ToList();
        foreach (var vm in toRemove)
        {
            vm.Dispose();
            Tabs.Remove(vm);
        }

        // Добавляем VM для новых бетономешалок
        foreach (var service in _registry.Mixers.Where(s => !mixers.Any(vm => vm.Service.Id == s.Id)))
        {
            Tabs.Insert(Tabs.Count > 0 ? Tabs.Count - 1 : 0, new MixerViewModel(service));
        }

        // Убеждаемся, что вкладка настроек всегда последняя
        if (!Tabs.Contains(_settingsTab))
        {
            Tabs.Add(_settingsTab);
        }
        else if (Tabs.IndexOf(_settingsTab) != Tabs.Count - 1)
        {
            Tabs.Move(Tabs.IndexOf(_settingsTab), Tabs.Count - 1);
        }

        SelectedTab ??= Tabs.FirstOrDefault();
    }
}
