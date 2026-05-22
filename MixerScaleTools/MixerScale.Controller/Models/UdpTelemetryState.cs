namespace MixerScale.Controller.Models;

internal sealed record UdpTelemetryState
{
    public bool Enabled { get; init; }
    public uint ScaleId { get; init; }
    public string TargetHost { get; init; } = string.Empty;
    public ushort Port { get; init; }
}
