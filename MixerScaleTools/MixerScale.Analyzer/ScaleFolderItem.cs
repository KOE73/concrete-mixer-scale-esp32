namespace MixerScale.Analyzer;

internal sealed record ScaleFolderItem(string Id, string Path)
{
    public override string ToString() => Id;
}
