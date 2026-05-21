namespace ScaleData.UdpRecorder;

internal sealed class PacketStats
{
    private ulong? _lastSequence;

    public long Accepted { get; private set; }

    public long BadPackets { get; private set; }

    public long Headers { get; private set; }

    public long Errors { get; private set; }

    public ulong LostPackets { get; private set; }

    public void RegisterPacket(SensorCsvPacket packet)
    {
        if (_lastSequence is { } last && packet.Sequence > last + 1)
        {
            LostPackets += packet.Sequence - last - 1;
        }

        _lastSequence = packet.Sequence;
        Accepted++;
    }

    public void RegisterBadPacket() => BadPackets++;

    public void RegisterHeader() => Headers++;

    public void RegisterError() => Errors++;
}
