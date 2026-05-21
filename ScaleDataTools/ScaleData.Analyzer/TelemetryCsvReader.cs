using System.Globalization;

namespace ScaleData.Analyzer;

internal static class TelemetryCsvReader
{
    public static TelemetryDataSet Read(IEnumerable<string> files)
    {
        var points = new List<TelemetryPoint>();
        var errors = new List<string>();

        foreach (var file in files)
        {
            var lineNumber = 0;
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var reader = new StreamReader(stream);
            string? line;
            while ((line = reader.ReadLine()) != null)
            {
                lineNumber++;
                if (string.IsNullOrWhiteSpace(line) || IsHeader(line))
                {
                    continue;
                }

                if (TryParse(line, out var point, out var error))
                {
                    points.Add(point);
                }
                else
                {
                    errors.Add($"{Path.GetFileName(file)}:{lineNumber}: {error}");
                }
            }
        }

        var ordered = points.OrderBy(point => point.Sequence).ToArray();
        return new TelemetryDataSet(ordered, FindLosses(ordered), errors);
    }

    private static bool TryParse(string line, out TelemetryPoint point, out string error)
    {
        point = default!;
        error = string.Empty;

        var parts = line.Split(',', StringSplitOptions.TrimEntries);
        if (parts.Length < 7)
        {
            error = "мало CSV-полей";
            return false;
        }

        if (!uint.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out var scaleId)
            || !ulong.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out var sequence)
            || !ulong.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var espMilliseconds))
        {
            error = "scale_id/seq/ms не число";
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
            // Fallback, если флаг не найден (например, старый или поврежденный формат)
            flagsIndex = parts.Length - 1;
        }

        if (flagsIndex < 5)
        {
            error = "некорректное положение поля flags";
            return false;
        }

        var rawCount = flagsIndex - 5;
        var raw = new long[rawCount];
        for (var i = 0; i < raw.Length; i++)
        {
            if (!long.TryParse(parts[i + 3], NumberStyles.Integer, CultureInfo.InvariantCulture, out raw[i]))
            {
                error = $"raw{i + 1} не число";
                return false;
            }
        }

        if (!long.TryParse(parts[flagsIndex - 2], NumberStyles.Integer, CultureInfo.InvariantCulture, out var rawSum)
            || !double.TryParse(parts[flagsIndex - 1], NumberStyles.Float, CultureInfo.InvariantCulture, out var kilogramsSum))
        {
            error = "raw_sum/kg_sum не число";
            return false;
        }

        var flags = parts[flagsIndex];

        var movingAverages = new List<(int WindowSizeSec, double ValueKg)>();
        for (var i = flagsIndex + 1; i < parts.Length - 1; i += 2)
        {
            if (int.TryParse(parts[i], NumberStyles.Integer, CultureInfo.InvariantCulture, out var win) &&
                double.TryParse(parts[i + 1], NumberStyles.Float, CultureInfo.InvariantCulture, out var val))
            {
                movingAverages.Add((win, val));
            }
        }

        point = new TelemetryPoint(scaleId, sequence, espMilliseconds, raw, rawSum, kilogramsSum, flags, movingAverages);
        return true;
    }

    private static bool IsHeader(string line) =>
        line.StartsWith("seq,", StringComparison.OrdinalIgnoreCase)
        || line.StartsWith("scale_id,", StringComparison.OrdinalIgnoreCase)
        || line.StartsWith("scaleId,", StringComparison.OrdinalIgnoreCase)
        || line.StartsWith("sequence,", StringComparison.OrdinalIgnoreCase);

    private static IReadOnlyList<SequenceLoss> FindLosses(IReadOnlyList<TelemetryPoint> points)
    {
        var losses = new List<SequenceLoss>();
        for (var i = 1; i < points.Count; i++)
        {
            var previous = points[i - 1].Sequence;
            var current = points[i].Sequence;
            if (current > previous + 1)
            {
                losses.Add(new SequenceLoss(previous, current, current - previous - 1));
            }
        }

        return losses;
    }
}
