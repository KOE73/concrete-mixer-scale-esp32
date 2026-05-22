using System.Formats.Cbor;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.Api;

internal static class CborLiveStateReader
{
    public static LiveWeightState Read(ReadOnlyMemory<byte> payload)
    {
        var reader = new CborReader(payload, CborConformanceMode.Lax);
        ulong sequence = 0;
        long timestampUs = 0;
        bool valid = false;
        bool cleanValid = false;
        string rejectReason = string.Empty;
        long rawSum = 0;
        long cleanSum = 0;
        double total = 0;
        double weight = 0;
        bool diagnosticPartialRead = false;
        TargetState? target = null;
        IReadOnlyList<MaState> ma = [];

        var pairs = ReadMapLength(reader);
        for (var i = 0; i < pairs; i++)
        {
            var key = reader.ReadTextString();
            switch (key)
            {
                case "sequence":
                    sequence = reader.ReadUInt64();
                    break;
                case "timestampUs":
                    timestampUs = reader.ReadInt64();
                    break;
                case "valid":
                    valid = reader.ReadBoolean();
                    break;
                case "cleanValid":
                    cleanValid = reader.ReadBoolean();
                    break;
                case "rejectReason":
                    rejectReason = reader.ReadTextString();
                    break;
                case "rawSum":
                    rawSum = reader.ReadInt64();
                    break;
                case "cleanSum":
                    cleanSum = reader.ReadInt64();
                    break;
                case "total":
                    total = reader.ReadDouble();
                    break;
                case "weight":
                    weight = reader.ReadDouble();
                    break;
                case "diagnosticPartialRead":
                    diagnosticPartialRead = reader.ReadBoolean();
                    break;
                case "target":
                    target = ReadTarget(reader);
                    break;
                case "ma":
                    ma = ReadMa(reader);
                    break;
                default:
                    reader.SkipValue();
                    break;
            }
        }
        reader.ReadEndMap();

        return new LiveWeightState
        {
            Sequence = sequence,
            TimestampUs = timestampUs,
            Valid = valid,
            CleanValid = cleanValid,
            RejectReason = rejectReason,
            RawSum = rawSum,
            CleanSum = cleanSum,
            Total = total,
            Weight = weight,
            DiagnosticPartialRead = diagnosticPartialRead,
            Target = target,
            Ma = ma
        };
    }

    private static TargetState ReadTarget(CborReader reader)
    {
        string stage = string.Empty;
        double weight = 0;
        double remaining = 0;
        double remainingShovels = 0;

        var pairs = ReadMapLength(reader);
        for (var i = 0; i < pairs; i++)
        {
            var key = reader.ReadTextString();
            switch (key)
            {
                case "stage":
                    stage = reader.ReadTextString();
                    break;
                case "weight":
                    weight = reader.ReadDouble();
                    break;
                case "remaining":
                    remaining = reader.ReadDouble();
                    break;
                case "remainingShovels":
                    remainingShovels = reader.ReadDouble();
                    break;
                default:
                    reader.SkipValue();
                    break;
            }
        }
        reader.ReadEndMap();

        return new TargetState
        {
            Stage = stage,
            Weight = weight,
            Remaining = remaining,
            RemainingShovels = remainingShovels
        };
    }

    private static IReadOnlyList<MaState> ReadMa(CborReader reader)
    {
        var count = ReadArrayLength(reader);
        var ma = new List<MaState>(count);
        for (var i = 0; i < count; i++)
        {
            ma.Add(ReadMaItem(reader));
        }
        reader.ReadEndArray();
        return ma;
    }

    private static MaState ReadMaItem(CborReader reader)
    {
        string name = string.Empty;
        bool valid = false;
        long rawSum = 0;
        double total = 0;
        double weight = 0;

        var pairs = ReadMapLength(reader);
        for (var i = 0; i < pairs; i++)
        {
            var key = reader.ReadTextString();
            switch (key)
            {
                case "name":
                    name = reader.ReadTextString();
                    break;
                case "valid":
                    valid = reader.ReadBoolean();
                    break;
                case "rawSum":
                    rawSum = reader.ReadInt64();
                    break;
                case "total":
                    total = reader.ReadDouble();
                    break;
                case "weight":
                    weight = reader.ReadDouble();
                    break;
                default:
                    reader.SkipValue();
                    break;
            }
        }
        reader.ReadEndMap();

        return new MaState
        {
            Name = name,
            Valid = valid,
            RawSum = rawSum,
            Total = total,
            Weight = weight
        };
    }

    private static int ReadMapLength(CborReader reader)
    {
        return reader.ReadStartMap()
               ?? throw new InvalidOperationException("Indefinite-length CBOR maps are not supported.");
    }

    private static int ReadArrayLength(CborReader reader)
    {
        return reader.ReadStartArray()
               ?? throw new InvalidOperationException("Indefinite-length CBOR arrays are not supported.");
    }
}
