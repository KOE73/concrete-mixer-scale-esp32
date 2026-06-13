using System.Text.Json;

namespace MixerScale.Controller.Configuration;

internal sealed record ControllerSettings
{
    public int PollIntervalMs { get; init; } = 1000;
    public int RequestTimeoutMs { get; init; } = 5000;

    /// <summary>
    /// Список бетономешалок, которые добавляются автоматически при старте приложения.
    /// </summary>
    public IReadOnlyList<InitialMixerSettings> InitialMixers { get; init; } =
    [
        new InitialMixerSettings
        {
            Name = "Бетономешалка",
            Endpoint = "http://192.168.20.41",
            Type = MixerType.Real
        }
    ];

    public static ControllerSettings Load()
    {
        var settingsPath = FindSettingsFile();
        if (!File.Exists(settingsPath))
        {
            return new ControllerSettings();
        }

        return JsonSerializer.Deserialize<ControllerSettings>(
            File.ReadAllText(settingsPath),
            new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                ReadCommentHandling = JsonCommentHandling.Skip,
                AllowTrailingCommas = true
            }) ?? new ControllerSettings();
    }

    private static string FindSettingsFile()
    {
        var currentDirectoryPath = Path.Combine(Environment.CurrentDirectory, "appsettings.json");
        return File.Exists(currentDirectoryPath)
            ? currentDirectoryPath
            : Path.Combine(AppContext.BaseDirectory, "appsettings.json");
    }
}

internal sealed record InitialMixerSettings
{
    public string Name { get; init; } = string.Empty;
    public string Endpoint { get; init; } = string.Empty;
    public MixerType Type { get; init; } = MixerType.Real;
}

internal enum MixerType
{
    Real,
    Emulator
}
