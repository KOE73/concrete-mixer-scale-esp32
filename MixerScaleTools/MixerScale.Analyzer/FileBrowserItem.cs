namespace MixerScale.Analyzer;

internal sealed record FileBrowserItem(string Path, long SizeBytes, DateTime LastWriteTime)
{
    public string Name => System.IO.Path.GetFileName(Path);

    public string SizeText => FormatSize(SizeBytes);

    public string DisplayText => $"{Name,-36} {SizeText,10}";

    public override string ToString() => DisplayText;

    private static string FormatSize(long bytes)
    {
        if (bytes < 1024)
        {
            return $"{bytes} B";
        }

        var value = bytes / 1024.0;
        if (value < 1024)
        {
            return $"{value:0.0} KB";
        }

        value /= 1024.0;
        return $"{value:0.0} MB";
    }
}
