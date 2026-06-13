using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace MixerScale.Controller.Views;

internal sealed partial class MetricsView : UserControl
{
    public MetricsView()
    {
        AvaloniaXamlLoader.Load(this);
    }
}
