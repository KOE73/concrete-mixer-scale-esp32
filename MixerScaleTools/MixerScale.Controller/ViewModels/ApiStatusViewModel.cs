using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using MixerScale.Controller.Models;
using MixerScale.Controller.Services;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Строки диагностики: статус последних API-вызовов, WiFi и UDP.
/// </summary>
internal sealed partial class ApiStatusViewModel : ObservableObject
{
    [ObservableProperty] private string _statusText   = string.Empty;
    [ObservableProperty] private string _wifiText     = string.Empty;
    [ObservableProperty] private string _udpText      = string.Empty;

    public void Update(
        IReadOnlyList<ApiCallStatus> statuses,
        WifiState? wifi,
        UdpTelemetryState? udp)
    {
        var sb = new StringBuilder();
        foreach (var s in statuses)
        {
            sb.AppendLine(s.ToString());
        }
        StatusText = sb.ToString().TrimEnd();

        WifiText = wifi is null
            ? "WiFi: -"
            : $"WiFi AP: {(wifi.Ap?.Started == true ? wifi.Ap.Ssid : "-")} | " +
              $"STA: {(wifi.Sta?.Connected == true ? wifi.Sta.Ip : "нет STA")}";

        UdpText = udp is null
            ? "UDP: -"
            : $"UDP: ID {udp.ScaleId} → {udp.TargetHost}:{udp.Port} [{(udp.Enabled ? "ON" : "OFF")}]";
    }
}
