using System.Text;

namespace MixerScale.UdpRecorder;

internal sealed class SessionCsvWriter : IAsyncDisposable
{
    private readonly CsvFileSettings _settings;
    private readonly Dictionary<uint, StreamWriter> _writers = new();
    private readonly Dictionary<uint, string> _filePaths = new();

    public SessionCsvWriter(CsvFileSettings settings)
    {
        _settings = settings;
        Directory.CreateDirectory(_settings.OutputDirectory);
    }

    public async ValueTask WriteAsync(SensorCsvPacket packet, CancellationToken cancellationToken)
    {
        var writer = await EnsureWriterAsync(packet, cancellationToken);
        await writer.WriteLineAsync(packet.ToCsvLine().AsMemory(), cancellationToken);
        await writer.FlushAsync(cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var writer in _writers.Values)
        {
            await writer.FlushAsync();
            await writer.DisposeAsync();
        }
    }

    private async ValueTask<StreamWriter> EnsureWriterAsync(SensorCsvPacket packet, CancellationToken cancellationToken)
    {
        if (_writers.TryGetValue(packet.ScaleId, out var existing))
        {
            return existing;
        }

        var filePath = ResolveSessionFilePath(packet.ScaleId);
        _filePaths[packet.ScaleId] = filePath;
        _settings.CurrentFilePath = filePath;

        var fileExists = File.Exists(filePath);
        var stream = new FileStream(filePath, FileMode.Append, FileAccess.Write, FileShare.Read);
        var writer = new StreamWriter(stream, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        _writers[packet.ScaleId] = writer;

        if (!fileExists || stream.Length == 0)
        {
            await writer.WriteLineAsync(packet.BuildHeader().AsMemory(), cancellationToken);
        }

        return writer;
    }

    private string ResolveSessionFilePath(uint scaleId)
    {
        if (!string.IsNullOrWhiteSpace(_settings.ExplicitFilePath))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(_settings.ExplicitFilePath))!);
            return Path.GetFullPath(_settings.ExplicitFilePath);
        }

        var scaleDirectory = Path.Combine(_settings.OutputDirectory, scaleId.ToString());
        Directory.CreateDirectory(scaleDirectory);

        var now = _settings.UseUtcDate ? DateTime.UtcNow : DateTime.Now;
        var baseName = $"{_settings.FilePrefix}-{now.ToString(_settings.TimestampFormat)}";
        var candidate = Path.Combine(scaleDirectory, $"{baseName}.csv");
        if (!File.Exists(candidate))
        {
            return candidate;
        }

        for (var index = 1; index < 10_000; index++)
        {
            candidate = Path.Combine(scaleDirectory, $"{baseName}-{index:000}.csv");
            if (!File.Exists(candidate))
            {
                return candidate;
            }
        }

        throw new InvalidOperationException("Не удалось подобрать свободное имя CSV-файла.");
    }
}
