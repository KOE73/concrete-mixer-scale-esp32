using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace MixerScale.Controller.ViewModels;

internal sealed partial class SettingsViewModel : ObservableObject
{
    public MainViewModel Main { get; }
    public string Header => "⚙ Настройка";

    public SettingsViewModel(MainViewModel main)
    {
        Main = main;
    }
}
