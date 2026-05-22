using System.Text.Json;

namespace MixerScale.Controller.Configuration;

internal sealed record ControllerSettings
{
    public string DeviceBaseUrl { get; init; } = "http://192.168.20.41";
    public int PollIntervalMs { get; init; } = 1000;
    public int RequestTimeoutMs { get; init; } = 5000;

    public Uri DeviceBaseUri => new(DeviceBaseUrl.TrimEnd('/') + "/");

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
