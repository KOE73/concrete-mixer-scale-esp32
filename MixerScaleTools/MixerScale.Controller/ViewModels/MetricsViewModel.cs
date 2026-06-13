using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.ViewModels;

/// <summary>
/// Верхняя панель метрик: seq, rawSum, cleanSum, основной MA, состояние семпла и признак связи.
/// </summary>
internal sealed partial class MetricsViewModel : ObservableObject
{
    private static readonly IBrush OnlineBrush  = new SolidColorBrush(Color.Parse("#22A06B"));
    private static readonly IBrush OfflineBrush = new SolidColorBrush(Color.Parse("#C9372C"));

    [ObservableProperty] private string _sequenceText   = "-";
    [ObservableProperty] private string _rawSumText     = "-";
    [ObservableProperty] private string _cleanSumText   = "-";
    [ObservableProperty] private string _primaryMaText  = "-";
    [ObservableProperty] private string _sampleStateText = "offline";
    [ObservableProperty] private string _endpointTooltip = string.Empty;
    [ObservableProperty] private IBrush _connectionBrush = OfflineBrush;

    public void Update(LiveWeightState? weight, bool isOnline)
    {
        ConnectionBrush = isOnline ? OnlineBrush : OfflineBrush;

        if (weight is null || !isOnline)
        {
            SequenceText    = "-";
            RawSumText      = "-";
            CleanSumText    = "-";
            PrimaryMaText   = "-";
            SampleStateText = "offline";
            return;
        }

        var primaryMa = weight.Ma.FirstOrDefault(m => m.Name == "ma_3s")
                     ?? weight.Ma.FirstOrDefault(m => m.Name.StartsWith("ma_", StringComparison.OrdinalIgnoreCase));

        SequenceText    = weight.Sequence.ToString();
        RawSumText      = weight.RawSum.ToString("N0");
        CleanSumText    = weight.CleanSum.ToString("N0");
        PrimaryMaText   = primaryMa is null || !primaryMa.Valid ? "-" : primaryMa.RawSum.ToString("N0");
        SampleStateText = weight.Valid
            ? (weight.CleanValid ? "valid" : $"reject: {weight.RejectReason}")
            : $"invalid: {weight.RejectReason}";
    }
}
