using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace MixerScale.Controller.Views;

internal sealed partial class AddRealMixerDialog : Window
{
    /// <summary>Результат диалога: (name, endpoint) или null если отменён.</summary>
    public (string Name, string Endpoint)? Result { get; private set; }

    private TextBox _nameBox     = null!;
    private TextBox _endpointBox = null!;

    public AddRealMixerDialog()
    {
        AvaloniaXamlLoader.Load(this);
        _nameBox     = this.FindControl<TextBox>("NameBox")!;
        _endpointBox = this.FindControl<TextBox>("EndpointBox")!;
    }

    private void AddClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        var name     = (_nameBox.Text ?? string.Empty).Trim();
        var endpoint = (_endpointBox.Text ?? string.Empty).Trim();

        if (string.IsNullOrWhiteSpace(endpoint))
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(name))
        {
            name = "Бетономешалка";
        }

        Result = (name, endpoint);
        Close();
    }

    private void CancelClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e) => Close();
}
