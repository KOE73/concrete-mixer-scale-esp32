namespace MixerScale.Controller.Api;

internal sealed record ApiCallResult<T>(
    bool Success,
    T? Value,
    TimeSpan Elapsed,
    string Error = "")
{
    public static ApiCallResult<T> Ok(T value, TimeSpan elapsed) => new(true, value, elapsed);

    public static ApiCallResult<T> Fail(string error, TimeSpan elapsed) => new(false, default, elapsed, error);
}
