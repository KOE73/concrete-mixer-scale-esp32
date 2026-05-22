namespace MixerScale.Analyzer;

internal sealed record TelemetryPoint(
    uint ScaleId,
    ulong Sequence,
    ulong EspMilliseconds,
    long RawSum,
    double KilogramsSum,
    string Flags,
    IReadOnlyList<(int WindowSizeSec, double ValueKg)> MovingAverages,
    long CleanSum = 0,
    bool CleanValid = false,
    string RejectReason = "",
    IReadOnlyList<(int WindowSizeSec, double ValueRaw)> RecomputedMovingAverages = null!)
{
    public IReadOnlyList<(int WindowSizeSec, double ValueRaw)> EffectiveMovingAverages =>
        RecomputedMovingAverages ?? Array.Empty<(int WindowSizeSec, double ValueRaw)>();
}

internal sealed record SequenceLoss(
    ulong AfterSequence,
    ulong BeforeSequence,
    ulong MissingCount);

internal sealed record TelemetryDataSet(
    IReadOnlyList<TelemetryPoint> Points,
    IReadOnlyList<SequenceLoss> LostPackets,
    IReadOnlyList<string> Errors)
{
    public static TelemetryDataSet Empty { get; } = new(Array.Empty<TelemetryPoint>(), Array.Empty<SequenceLoss>(), Array.Empty<string>());
}
