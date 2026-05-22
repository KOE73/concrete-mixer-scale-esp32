namespace MixerScale.Controller.Models;

internal sealed record DeviceSettingsState
{
    public long SumOffset { get; init; }
    public double SumScale { get; init; }
    public IReadOnlyList<UnitConversionState> Units { get; init; } = [];
    public IReadOnlyList<SetpointState> Setpoints { get; init; } = [];
}

internal sealed record UnitConversionState
{
    public string Name { get; init; } = string.Empty;
    public double RawPerUnit { get; init; }
}

internal sealed record SetpointState
{
    public string Name { get; init; } = string.Empty;
    public long RawValue { get; init; }
}
