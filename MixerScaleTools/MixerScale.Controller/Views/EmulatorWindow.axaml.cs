using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace MixerScale.Controller.Views;

internal sealed partial class EmulatorWindow : Window
{
    public EmulatorWindow()
    {
        AvaloniaXamlLoader.Load(this);
    }
}
