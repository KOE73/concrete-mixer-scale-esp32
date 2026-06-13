using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using MixerScale.Controller.ViewModels;

namespace MixerScale.Controller.Views;

internal sealed partial class GraphView : UserControl
{
    private readonly GraphRenderControl _renderControl = new();
    private GraphViewModel? _vm;

    public GraphView()
    {
        AvaloniaXamlLoader.Load(this);
        var host = this.FindControl<ContentControl>("GraphHost")!;
        host.Content = _renderControl;
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);

        if (_vm is not null)
        {
            _vm.PropertyChanged -= OnVmPropertyChanged;
        }

        _vm = DataContext as GraphViewModel;

        if (_vm is not null)
        {
            _vm.PropertyChanged += OnVmPropertyChanged;
            PushToRender();
        }
    }

    private void OnVmPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(GraphViewModel.Series)
                           or nameof(GraphViewModel.Markers)
                           or nameof(GraphViewModel.DurationSeconds))
        {
            PushToRender();
        }
    }

    private void PushToRender()
    {
        if (_vm is null) return;
        _renderControl.Series   = _vm.Series;
        _renderControl.Markers  = _vm.Markers;
        _renderControl.Duration = TimeSpan.FromSeconds(Math.Max(5, _vm.DurationSeconds));
        _renderControl.InvalidateVisual();
    }
}
