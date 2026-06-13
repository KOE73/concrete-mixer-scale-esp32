using Avalonia.Controls;
using Avalonia.Interactivity;

namespace MixerScale.Controller.Views;

public partial class DashboardWindow : Window
{
    public DashboardWindow()
    {
        InitializeComponent();
    }

    private void OnCloseClick(object? sender, RoutedEventArgs e)
    {
        Close();
    }
}
