namespace MixerScale.Controller.Services;

/// <summary>
/// Запись о результате одного API-вызова. Success=null означает «ещё не вызывался».
/// </summary>
internal sealed record ApiCallStatus(string Name, bool? Success, TimeSpan Elapsed, string Error = "")
{
    public override string ToString() =>
        Success is null
            ? $"{Name,-30} -"
            : Success.Value
                ? $"{Name,-30} OK {Elapsed.TotalMilliseconds:0} ms"
                : $"{Name,-30} ERR {Short(Error)}";

    private static string Short(string s) =>
        string.IsNullOrWhiteSpace(s) ? "-" : s.Length <= 60 ? s : s[..57] + "...";
}
