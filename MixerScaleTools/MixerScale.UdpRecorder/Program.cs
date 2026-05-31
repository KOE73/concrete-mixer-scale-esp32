using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using MixerScale.UdpRecorder;
using Spectre.Console;
using Spectre.Console.Rendering;

var settings = RecorderSettings.Load(args);
var outputDirectory = settings.OutputDirectory ?? settings.DataDirectory;
Directory.CreateDirectory(outputDirectory);

AnsiConsole.MarkupLine("[bold]MixerScale UDP recorder[/]");
AnsiConsole.MarkupLine($"UDP: [yellow]{settings.BindAddress}:{settings.Port}[/]");
AnsiConsole.MarkupLine($"Listen endpoints: [yellow]{Markup.Escape(FormatListenEndpoints(settings))}[/]");
AnsiConsole.MarkupLine($"CSV: [yellow]{Path.GetFullPath(outputDirectory)}[/]");
AnsiConsole.MarkupLine("Ожидаемый пакет: [grey]scale_id,seq,ms,raw_sum,kg_sum,flags[/]");
AnsiConsole.MarkupLine("Остановка: [grey]Ctrl+C[/]");

using var stopping = new CancellationTokenSource();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    stopping.Cancel();
};

await using var writer = new SessionCsvWriter(settings.Csv);
using var udp = new UdpClient(new IPEndPoint(IPAddress.Parse(settings.BindAddress), settings.Port));
var stats = new PacketStats();
SensorCsvPacket? lastPacket = null;
var lastRemote = "-";
var lastError = "-";

if (Console.IsOutputRedirected)
{
    await ReceiveLoopAsync(() => { });
}
else
{
    await AnsiConsole.Live(CreateView(settings, stats, lastPacket, lastRemote, lastError))
        .AutoClear(false)
        .Overflow(VerticalOverflow.Ellipsis)
        .Cropping(VerticalOverflowCropping.Top)
        .StartAsync(async context =>
        {
            await ReceiveLoopAsync(() => context.UpdateTarget(CreateView(settings, stats, lastPacket, lastRemote, lastError)));
        });
}

async Task ReceiveLoopAsync(Action updateView)
{
    while (!stopping.IsCancellationRequested)
    {
        try
        {
            var receiveTask = udp.ReceiveAsync(stopping.Token).AsTask();
            var result = await receiveTask;
            lastRemote = result.RemoteEndPoint.ToString();

            var text = Encoding.UTF8.GetString(result.Buffer).Trim();
            if (SensorCsvPacket.IsHeader(text))
            {
                stats.RegisterHeader();
                updateView();
                continue;
            }

            if (!SensorCsvPacket.TryParse(text, out var packet, out var error))
            {
                lastError = error;
                stats.RegisterBadPacket();
                updateView();
                continue;
            }

            await writer.WriteAsync(packet, stopping.Token);
            lastPacket = packet;
            lastError = "-";
            stats.RegisterPacket(packet);
            updateView();
        }
        catch (OperationCanceledException)
        {
            break;
        }
        catch (Exception ex)
        {
            lastError = ex.Message;
            stats.RegisterError();
            updateView();
            await Task.Delay(250, stopping.Token).ContinueWith(_ => { });
        }
    }
}

static IRenderable CreateView(
    RecorderSettings settings,
    PacketStats stats,
    SensorCsvPacket? lastPacket,
    string lastRemote,
    string lastError)
{
    var grid = new Grid();
    grid.AddColumn();
    grid.AddRow(CreateStatusTable(settings, stats, lastPacket, lastRemote, lastError));
    grid.AddRow(CreatePacketTable(lastPacket));
    return grid;
}

static Table CreateStatusTable(
    RecorderSettings settings,
    PacketStats stats,
    SensorCsvPacket? lastPacket,
    string lastRemote,
    string lastError)
{
    var table = new Table().Border(TableBorder.Rounded).Title("UDP / CSV");
    table.AddColumn("Параметр");
    table.AddColumn("Значение");

    table.AddRow("Порт", settings.Port.ToString());
    table.AddRow("Файл", Markup.Escape(Path.GetFileName(settings.Csv.CurrentFilePath)));
    table.AddRow("Папка", Markup.Escape(Path.GetFullPath(settings.OutputDirectory ?? settings.DataDirectory)));
    table.AddRow("Последний отправитель", Markup.Escape(lastRemote));
    table.AddRow("Принято", stats.Accepted.ToString());
    table.AddRow("Плохих строк", stats.BadPackets.ToString());
    table.AddRow("Заголовков CSV", stats.Headers.ToString());
    table.AddRow("Потери seq", stats.LostPackets.ToString());
    table.AddRow("Последний seq", lastPacket?.Sequence.ToString() ?? "-");
    table.AddRow("Scale ID", lastPacket?.ScaleId.ToString() ?? "-");
    table.AddRow("Listen endpoints", Markup.Escape(FormatListenEndpoints(settings)));
    table.AddRow("Ошибка", Markup.Escape(lastError));

    return table;
}

static string FormatListenEndpoints(RecorderSettings settings)
{
    if (!IPAddress.TryParse(settings.BindAddress, out var bindAddress))
    {
        return $"{settings.BindAddress}:{settings.Port}";
    }

    if (!IPAddress.Any.Equals(bindAddress))
    {
        return $"{bindAddress}:{settings.Port}";
    }

    var endpoints = NetworkInterface.GetAllNetworkInterfaces()
        .Where(networkInterface =>
            networkInterface.OperationalStatus == OperationalStatus.Up &&
            networkInterface.NetworkInterfaceType != NetworkInterfaceType.Loopback)
        .SelectMany(networkInterface =>
            networkInterface.GetIPProperties().UnicastAddresses
                .Where(address =>
                    address.Address.AddressFamily == AddressFamily.InterNetwork &&
                    !IPAddress.IsLoopback(address.Address))
                .Select(address => $"{address.Address}:{settings.Port}"))
        .Distinct()
        .Order()
        .ToArray();

    return endpoints.Length == 0 ? $"0.0.0.0:{settings.Port}" : string.Join(", ", endpoints);
}

static Table CreatePacketTable(SensorCsvPacket? packet)
{
    var table = new Table().Border(TableBorder.Simple).Title("Последний пакет");
    table.AddColumn("Поле");
    table.AddColumn("Значение");

    if (packet is null)
    {
        table.AddRow("данные", "пока нет");
        return table;
    }

    table.AddRow("seq", packet.Sequence.ToString());
    table.AddRow("scale_id", packet.ScaleId.ToString());
    table.AddRow("ms", packet.EspMilliseconds.ToString());
    table.AddRow("raw_sum", packet.RawSum.ToString());
    table.AddRow("kg_sum", packet.KilogramsSum.ToString("0.###"));
    table.AddRow("flags", Markup.Escape(packet.Flags));

    return table;
}
