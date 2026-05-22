using System.Text.Json;
using System.Text.Json.Serialization;

namespace MixerScale.UdpRecorder;

internal sealed record RecorderSettings
{
    public string BindAddress { get; init; } = "0.0.0.0";

    public int Port { get; init; } = 4222;

    public string DataDirectory { get; init; } = @"C:\ScaleData";

    public string? OutputDirectory { get; init; }

    public CsvFileSettings Csv { get; init; } = new();

    public static RecorderSettings Load(string[] args)
    {
        var settingsPath = GetArgValue(args, "--config") ?? FindSettingsFile();
        var settings = File.Exists(settingsPath)
            ? JsonSerializer.Deserialize<RecorderSettings>(
                File.ReadAllText(settingsPath),
                JsonOptions()) ?? new RecorderSettings()
            : new RecorderSettings();

        var bindAddress = GetArgValue(args, "--ip") ?? settings.BindAddress;
        var dataDirectory = GetArgValue(args, "--data") ?? settings.DataDirectory;
        var outputDirectory = GetArgValue(args, "--output") ?? settings.OutputDirectory ?? dataDirectory;
        var port = int.TryParse(GetArgValue(args, "--port"), out var parsedPort)
            ? parsedPort
            : settings.Port;
        var explicitFilePath = GetArgValue(args, "--file") ?? settings.Csv.ExplicitFilePath;

        var csv = settings.Csv with
        {
            OutputDirectory = outputDirectory,
            ExplicitFilePath = explicitFilePath
        };

        return settings with
        {
            BindAddress = bindAddress,
            Port = port,
            DataDirectory = dataDirectory,
            OutputDirectory = outputDirectory,
            Csv = csv
        };
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

    private static string? GetArgValue(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (string.Equals(args[i], name, StringComparison.OrdinalIgnoreCase))
            {
                return args[i + 1];
            }
        }

        return null;
    }

    private static JsonSerializerOptions JsonOptions() => new()
    {
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
        PropertyNameCaseInsensitive = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };
}
