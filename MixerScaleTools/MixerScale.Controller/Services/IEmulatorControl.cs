namespace MixerScale.Controller.Services;

/// <summary>
/// Дополнительный интерфейс, который реализует только EmulatorScaleService.
/// Позволяет окну настройки эмулятора управлять симулируемым весом.
/// </summary>
internal interface IEmulatorControl
{
    long RawSum { get; set; }
    long RawMin { get; }
    long RawMax { get; }
}
