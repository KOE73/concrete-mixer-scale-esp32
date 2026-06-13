using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;
using MixerScale.Controller.Views;

namespace MixerScale.Controller;

internal sealed class App : Application
{
    public override void Initialize()
    {
        Styles.Add(new FluentTheme());

        // Глобальный компактный стиль для TextBox
        var textBoxStyle = new Style(x => x.OfType<TextBox>());
        textBoxStyle.Setters.Add(new Setter(TextBox.PaddingProperty, new Thickness(4, 2)));
        textBoxStyle.Setters.Add(new Setter(TextBox.MinHeightProperty, 20.0));
        Styles.Add(textBoxStyle);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var settings = Configuration.ControllerSettings.Load();
            var registry = new Services.MixerRegistry();

            foreach (var mixer in settings.InitialMixers)
            {
                if (mixer.Type == Configuration.MixerType.Real)
                {
                    registry.AddRealMixer(mixer.Name, mixer.Endpoint, settings.PollIntervalMs, settings.RequestTimeoutMs);
                }
                else
                {
                    registry.AddEmulatorMixer(mixer.Name);
                }
            }

            var mainVm = new ViewModels.MainViewModel(registry);

            desktop.MainWindow = new MainWindow
            {
                DataContext = mainVm
            };

            desktop.Exit += (_, _) => registry.Dispose();
        }

        base.OnFrameworkInitializationCompleted();
    }
}
