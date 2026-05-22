using System.Globalization;

namespace MixerScale.Analyzer;

internal static class TelemetryCsvReader
{
    public static TelemetryDataSet Read(IEnumerable<string> files)
    {
        var settings = AnalyzerSettings.Load();
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

        var ordered = RecomputeCleanAndMovingAverages(
            points.OrderBy(point => point.Sequence).ToArray(),
            settings.HardRejectDeltaRawSum,
            settings.MovingAverageWindowsSeconds);
        return new TelemetryDataSet(ordered, FindLosses(ordered), errors);
    }

    private static bool TryParse(string line, out TelemetryPoint point, out string error)
    {
        point = default!;
        error = string.Empty;

        var parts = line.Split(',', StringSplitOptions.TrimEntries);
        if (parts.Length < 6)
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

        point = new TelemetryPoint(scaleId, sequence, espMilliseconds, rawSum, kilogramsSum, flags, movingAverages);
        return true;
    }

    private static TelemetryPoint[] RecomputeCleanAndMovingAverages(
        IReadOnlyList<TelemetryPoint> points,
        long hardRejectDeltaRawSum,
        IReadOnlyList<int> movingAverageWindowsSeconds)
    {
        var windows = movingAverageWindowsSeconds
            .Where(window => window > 0)
            .Distinct()
            .OrderBy(window => window)
            .ToArray();
        var states = windows.ToDictionary(window => window, _ => new TimeWindowAverage());
        var result = new TelemetryPoint[points.Count];
        var hasLastGood = false;
        long lastGoodCleanSum = 0;

        for (var i = 0; i < points.Count; i++)
        {
            var point = points[i];
            var firmwareInvalid = point.Flags.Contains("invalid", StringComparison.OrdinalIgnoreCase)
                && !point.Flags.Contains("hard_reject", StringComparison.OrdinalIgnoreCase);
            var cleanValid = false;
            var rejectReason = string.Empty;
            var cleanSum = point.RawSum;

            if (firmwareInvalid)
            {
                rejectReason = ExtractRejectReason(point.Flags, "sensor_error");
                cleanSum = hasLastGood ? lastGoodCleanSum : point.RawSum;
            }
            else if (hasLastGood && Math.Abs(point.RawSum - lastGoodCleanSum) > hardRejectDeltaRawSum)
            {
                rejectReason = "hard_reject";
                cleanSum = lastGoodCleanSum;
            }
            else
            {
                cleanValid = true;
                cleanSum = point.RawSum;
                hasLastGood = true;
                lastGoodCleanSum = cleanSum;
                foreach (var state in states.Values)
                {
                    state.Add(point.EspMilliseconds, cleanSum);
                }
            }

            var ma = new List<(int WindowSizeSec, double ValueRaw)>();
            foreach (var window in windows)
            {
                var state = states[window];
                state.Trim(point.EspMilliseconds, (ulong)window * 1000UL);
                if (state.Count > 0)
                {
                    ma.Add((window, state.Average));
                }
            }

            result[i] = point with
            {
                CleanSum = cleanSum,
                CleanValid = cleanValid,
                RejectReason = rejectReason,
                RecomputedMovingAverages = ma
            };
        }

        return result;
    }

    private static string ExtractRejectReason(string flags, string fallback)
    {
        var parts = flags.Split('|', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return parts.FirstOrDefault(part => !part.Equals("valid", StringComparison.OrdinalIgnoreCase)
            && !part.Equals("invalid", StringComparison.OrdinalIgnoreCase)) ?? fallback;
    }

    private sealed class TimeWindowAverage
    {
        private readonly Queue<(ulong TimeMs, long Value)> _values = new();
        private long _sum;

        public int Count => _values.Count;
        public double Average => _values.Count == 0 ? double.NaN : (double)_sum / _values.Count;

        public void Add(ulong timeMs, long value)
        {
            _values.Enqueue((timeMs, value));
            _sum += value;
        }

        public void Trim(ulong currentTimeMs, ulong windowMs)
        {
            while (_values.Count > 0 && currentTimeMs > _values.Peek().TimeMs + windowMs)
            {
                _sum -= _values.Dequeue().Value;
            }
        }
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
