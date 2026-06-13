using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;

namespace MixerScale.Controller.Views;

internal sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        AvaloniaXamlLoader.Load(this);
    }

    private async void OnAddRealClick(object? sender, RoutedEventArgs e)
    {
        var dialog = new AddRealMixerDialog();
        await dialog.ShowDialog(this);

        if (dialog.Result is { } res && DataContext is ViewModels.MainViewModel vm)
        {
            vm.AddRealCommand.Execute(res.Endpoint);
        }
    }
}
