using System.Globalization;

namespace ScaleData.UdpRecorder;

internal sealed record SensorCsvPacket(
    uint ScaleId,
    ulong Sequence,
    ulong EspMilliseconds,
    long[] RawValues,
    long RawSum,
    double KilogramsSum,
    string Flags,
    IReadOnlyList<string> ExtraFields)
{
    public static bool IsHeader(string line)
    {
        var trimmed = line.Trim();
        return trimmed.StartsWith("scale_id,", StringComparison.OrdinalIgnoreCase)
            || trimmed.StartsWith("scaleId,", StringComparison.OrdinalIgnoreCase)
            || trimmed.StartsWith("seq,", StringComparison.OrdinalIgnoreCase)
            || trimmed.StartsWith("sequence,", StringComparison.OrdinalIgnoreCase);
    }

    public static bool TryParse(string line, out SensorCsvPacket packet, out string error)
    {
        packet = default!;
        error = string.Empty;

        var parts = line.Split(',', StringSplitOptions.TrimEntries);
        if (parts.Length < 7)
        {
            error = "мало CSV-полей";
            return false;
        }

        if (!uint.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out var scaleId) || scaleId == 0)
        {
            error = "scale_id не число";
            return false;
        }

        if (!ulong.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out var sequence))
        {
            error = "seq не число";
            return false;
        }

        if (!ulong.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var espMilliseconds))
        {
            error = "ms не число";
            return false;
        }

        // Находим поле flags, которое содержит "valid" или "invalid"
        var flagsIndex = -1;
        for (var i = 3; i < parts.Length; i++)
        {
            if (parts[i].Contains("valid", StringComparison.OrdinalIgnoreCase) || parts[i].Contains("invalid", StringComparison.OrdinalIgnoreCase))
            {
                flagsIndex = i;
                break;
            }
        }

        if (flagsIndex == -1)
        {
            // Fallback
            flagsIndex = parts.Length - 1;
        }

        if (flagsIndex < 5)
        {
            error = "некорректное положение поля flags";
            return false;
        }

        var rawCount = flagsIndex - 5;
        if (rawCount <= 0)
        {
            error = "нет raw-каналов";
            return false;
        }

        var raw = new long[rawCount];
        for (var i = 0; i < raw.Length; i++)
        {
            if (!long.TryParse(parts[i + 3], NumberStyles.Integer, CultureInfo.InvariantCulture, out raw[i]))
            {
                error = $"raw{i + 1} не число";
                return false;
            }
        }

        if (!long.TryParse(parts[flagsIndex - 2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var rawSum))
        {
            error = "raw_sum не число";
            return false;
        }

        if (!double.TryParse(parts[flagsIndex - 1], NumberStyles.Float, CultureInfo.InvariantCulture, out var kilogramsSum))
        {
            error = "kg_sum не число";
            return false;
        }

        var extraFields = new List<string>();
        for (var i = flagsIndex + 1; i < parts.Length; i++)
        {
            extraFields.Add(parts[i]);
        }

        packet = new SensorCsvPacket(scaleId, sequence, espMilliseconds, raw, rawSum, kilogramsSum, parts[flagsIndex], extraFields);
        return true;
    }

    public string BuildHeader()
    {
        var rawHeaders = Enumerable.Range(1, RawValues.Length).Select(i => $"raw{i}");
        var baseHeaders = new[] { "scale_id", "seq", "ms" }.Concat(rawHeaders).Concat(new[] { "raw_sum", "kg_sum", "flags" });
        if (ExtraFields.Count > 0)
        {
            var extra = new string[ExtraFields.Count];
            for (int i = 0; i < ExtraFields.Count; i += 2)
            {
                int pairIdx = (i / 2) + 1;
                extra[i] = $"win{pairIdx}";
                if (i + 1 < ExtraFields.Count)
                {
                    extra[i + 1] = $"ma{pairIdx}";
                }
            }
            baseHeaders = baseHeaders.Concat(extra);
        }
        return string.Join(',', baseHeaders);
    }

    public string ToCsvLine()
    {
        var rawValues = RawValues.Select(value => value.ToString(CultureInfo.InvariantCulture));
        var baseParts = new[]
        {
            ScaleId.ToString(CultureInfo.InvariantCulture),
            Sequence.ToString(CultureInfo.InvariantCulture),
            EspMilliseconds.ToString(CultureInfo.InvariantCulture)
        }
            .Concat(rawValues)
            .Concat(new[]
            {
                RawSum.ToString(CultureInfo.InvariantCulture),
                KilogramsSum.ToString("0.###", CultureInfo.InvariantCulture),
                EscapeCsv(Flags)
            });

        if (ExtraFields.Count > 0)
        {
            baseParts = baseParts.Concat(ExtraFields.Select(EscapeCsv));
        }

        return string.Join(',', baseParts);
    }

    private static string EscapeCsv(string value)
    {
        if (!value.Contains(',') && !value.Contains('"') && !value.Contains('\n') && !value.Contains('\r'))
        {
            return value;
        }

        return '"' + value.Replace("\"", "\"\"", StringComparison.Ordinal) + '"';
    }
}
