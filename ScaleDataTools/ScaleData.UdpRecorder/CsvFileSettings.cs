namespace ScaleData.UdpRecorder;

internal sealed record CsvFileSettings
{
    public string OutputDirectory { get; init; } = Path.Combine("data", "udp");

    public string FilePrefix { get; init; } = "scale-raw";

    public bool UseUtcDate { get; init; }

    public string TimestampFormat { get; init; } = "yyyyMMdd-HHmmss";

    public string? ExplicitFilePath { get; init; }

    public string CurrentFilePath { get; set; } = string.Empty;
}
