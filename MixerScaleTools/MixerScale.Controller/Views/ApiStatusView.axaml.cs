using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace MixerScale.Controller.Views;

internal sealed partial class ApiStatusView : UserControl
{
    public ApiStatusView()
    {
        AvaloniaXamlLoader.Load(this);
    }
}
