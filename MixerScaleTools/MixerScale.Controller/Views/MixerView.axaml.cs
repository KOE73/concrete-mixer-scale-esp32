using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using MixerScale.Controller.ViewModels;

namespace MixerScale.Controller.Views;

internal sealed partial class MixerView : UserControl
{
    public MixerView()
    {
        AvaloniaXamlLoader.Load(this);
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);

        // Подписываемся на запросы открытия окон
        if (DataContext is MixerViewModel vm)
        {
            vm.EmulatorWindowRequested += OpenEmulatorWindow;
            vm.DashboardFullscreenRequested += OpenDashboardWindow;
        }
    }

    private void OpenEmulatorWindow(EmulatorViewModel emVm)
    {
        var window = new EmulatorWindow { DataContext = emVm };

        // Открываем как независимое окно (не диалог), чтобы не блокировать главное
        var parent = TopLevel.GetTopLevel(this) as Window;
        if (parent is not null)
        {
            window.Show(parent);
        }
        else
        {
            window.Show();
        }
    }

    private void OpenDashboardWindow(DashboardViewModel dashVm)
    {
        var window = new DashboardWindow { DataContext = dashVm };

        var parent = TopLevel.GetTopLevel(this) as Window;
        if (parent is not null)
        {
            window.ShowDialog(parent); // Модально, чтобы не кликать мимо дашборда
        }
        else
        {
            window.Show();
        }
    }
}
