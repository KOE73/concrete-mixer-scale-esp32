namespace MixerScale.Controller.Models;

internal sealed record WifiState
{
    public AccessPointState? Ap { get; init; }
    public StationState? Sta { get; init; }
}

internal sealed record AccessPointState
{
    public bool Started { get; init; }
    public string Ssid { get; init; } = string.Empty;
    public string Mac { get; init; } = string.Empty;
}

internal sealed record StationState
{
    public bool Configured { get; init; }
    public bool Connected { get; init; }
    public string Ssid { get; init; } = string.Empty;
    public string Ip { get; init; } = string.Empty;
    public string Mac { get; init; } = string.Empty;
    public bool HasPassword { get; init; }
}
