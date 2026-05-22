using System.Text.Json;

namespace MixerScale.Analyzer;

internal sealed record AnalyzerSettings
{
    public string DataDirectory { get; init; } = @"C:\ScaleData";
    public string DeviceBaseUrl { get; init; } = "http://192.168.4.1";
    public long HardRejectDeltaRawSum { get; init; } = 1_000_000;
    public int[] MovingAverageWindowsSeconds { get; init; } = [1, 3, 5, 10, 30, 60];

    public static AnalyzerSettings Load()
    {
        var settingsPath = FindSettingsFile();
        if (!File.Exists(settingsPath))
        {
            return new AnalyzerSettings();
        }

        return JsonSerializer.Deserialize<AnalyzerSettings>(
            File.ReadAllText(settingsPath),
            new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                ReadCommentHandling = JsonCommentHandling.Skip,
                AllowTrailingCommas = true
            }) ?? new AnalyzerSettings();
    }

    private static string FindSettingsFile()
    {
        var currentDirectoryPath = Path.Combine(Environment.CurrentDirectory, "appsettings.json");
        if (File.Exists(currentDirectoryPath))
        {
            return currentDirectoryPath;
        }

        return Path.Combine(AppContext.BaseDirectory, "appsettings.json");
    }
}
