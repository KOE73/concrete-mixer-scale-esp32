using System.Diagnostics;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using MixerScale.Controller.Configuration;
using MixerScale.Controller.Models;

namespace MixerScale.Controller.Api;

internal sealed class MixerScaleApiClient : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true
    };

    private readonly HttpClient _httpClient;

    public MixerScaleApiClient(ControllerSettings settings)
    {
        var handler = new HttpClientHandler
        {
            UseProxy = false
        };

        _httpClient = new HttpClient(handler)
        {
            BaseAddress = settings.DeviceBaseUri,
            Timeout = TimeSpan.FromMilliseconds(settings.RequestTimeoutMs)
        };
    }

    public Task<ApiCallResult<LiveWeightState>> GetStateAsync(CancellationToken cancellationToken) =>
        GetCborStateAsync(cancellationToken);

    public Task<ApiCallResult<DeviceSettingsState>> GetSettingsAsync(CancellationToken cancellationToken) =>
        GetJsonAsync<DeviceSettingsState>("api/settings", cancellationToken);

    public Task<ApiCallResult<DeviceSettingsState>> SaveSettingsAsync(
        DeviceSettingsState settings,
        CancellationToken cancellationToken) =>
        PostJsonAsync("api/settings", settings, cancellationToken);

    public Task<ApiCallResult<WifiState>> GetWifiAsync(CancellationToken cancellationToken) =>
        GetJsonAsync<WifiState>("api/wifi", cancellationToken);

    public Task<ApiCallResult<UdpTelemetryState>> GetUdpTelemetryAsync(CancellationToken cancellationToken) =>
        GetJsonAsync<UdpTelemetryState>("api/udp-telemetry", cancellationToken);

    public async Task<ApiCallResult<LiveWeightState>> GetCborStateAsync(CancellationToken cancellationToken)
    {
        var watch = Stopwatch.StartNew();
        try
        {
            using var response = await _httpClient.GetAsync("api/state.cbor", cancellationToken);
            response.EnsureSuccessStatusCode();
            await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
            var state = CborLiveStateReader.Read(await ReadAllBytesAsync(stream, cancellationToken));
            return ApiCallResult<LiveWeightState>.Ok(state, watch.Elapsed);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or InvalidOperationException)
        {
            return ApiCallResult<LiveWeightState>.Fail(ex.Message, watch.Elapsed);
        }
    }

    public void Dispose() => _httpClient.Dispose();

    private async Task<ApiCallResult<T>> GetJsonAsync<T>(string path, CancellationToken cancellationToken)
    {
        var watch = Stopwatch.StartNew();
        try
        {
            var value = await _httpClient.GetFromJsonAsync<T>(path, JsonOptions, cancellationToken);
            return value is null
                ? ApiCallResult<T>.Fail("Пустой ответ.", watch.Elapsed)
                : ApiCallResult<T>.Ok(value, watch.Elapsed);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException or NotSupportedException)
        {
            return ApiCallResult<T>.Fail(ex.Message, watch.Elapsed);
        }
    }

    private async Task<ApiCallResult<T>> PostJsonAsync<T>(
        string path,
        T payload,
        CancellationToken cancellationToken)
    {
        var watch = Stopwatch.StartNew();
        try
        {
            var json = JsonSerializer.Serialize(payload, JsonOptions);
            using var content = new StringContent(json, Encoding.UTF8, "application/json");
            using var response = await _httpClient.PostAsync(path, content, cancellationToken);
            if (!response.IsSuccessStatusCode)
            {
                var error = await response.Content.ReadAsStringAsync(cancellationToken);
                return ApiCallResult<T>.Fail(
                    string.IsNullOrWhiteSpace(error)
                        ? $"{(int)response.StatusCode} {response.ReasonPhrase}"
                        : $"{(int)response.StatusCode} {response.ReasonPhrase}: {error}",
                    watch.Elapsed);
            }

            var value = await response.Content.ReadFromJsonAsync<T>(JsonOptions, cancellationToken);
            return value is null
                ? ApiCallResult<T>.Fail("Пустой ответ.", watch.Elapsed)
                : ApiCallResult<T>.Ok(value, watch.Elapsed);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException or NotSupportedException)
        {
            return ApiCallResult<T>.Fail(ex.Message, watch.Elapsed);
        }
    }

    private static async Task<byte[]> ReadAllBytesAsync(Stream stream, CancellationToken cancellationToken)
    {
        using var memory = new MemoryStream();
        await stream.CopyToAsync(memory, cancellationToken);
        return memory.ToArray();
    }
}
