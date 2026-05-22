namespace MixerScale.Controller.Models;

internal sealed record LiveWeightState
{
    public ulong Sequence { get; init; }
    public long TimestampUs { get; init; }
    public bool Valid { get; init; }
    public bool CleanValid { get; init; }
    public string RejectReason { get; init; } = string.Empty;
    public long RawSum { get; init; }
    public long CleanSum { get; init; }
    public double Total { get; init; }
    public double Weight { get; init; }
    public bool DiagnosticPartialRead { get; init; }
    public TargetState? Target { get; init; }
    public IReadOnlyList<MaState> Ma { get; init; } = [];
}

internal sealed record TargetState
{
    public string Stage { get; init; } = string.Empty;
    public double Weight { get; init; }
    public double Remaining { get; init; }
    public double RemainingShovels { get; init; }
}

internal sealed record MaState
{
    public string Name { get; init; } = string.Empty;
    public bool Valid { get; init; }
    public long RawSum { get; init; }
    public double Total { get; init; }
    public double Weight { get; init; }
}
