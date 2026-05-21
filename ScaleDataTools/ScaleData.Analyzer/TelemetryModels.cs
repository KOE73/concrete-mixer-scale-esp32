namespace ScaleData.Analyzer;

internal sealed record TelemetryPoint(
    uint ScaleId,
    ulong Sequence,
    ulong EspMilliseconds,
    long[] RawValues,
    long RawSum,
    double KilogramsSum,
    string Flags,
    IReadOnlyList<(int WindowSizeSec, double ValueKg)> MovingAverages);

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
