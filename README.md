# Concrete Mixer Scale ESP32

Firmware skeleton for repeatable concrete batches on ESP32-S3 with several HX711 load-cell channels, a replaceable display layer and a small Web UI/API.

## Current Shape

- PlatformIO + ESP-IDF project.
- Partition table targets the Matrix Portal S3 documented flash size: 8 MB.
- HX711 channels and the read driver are configured in `include/config/hardware_config.hpp`.
- `config::Hx711ReadDriver::EspIdfLibSequential` uses `esp-idf-lib/hx711` from `src/idf_component.yml` and reads ready channels one by one.
- `config::Hx711ReadDriver::SharedClockBus` is the near-simultaneous path for a shared SCK wire. All enabled HX711 channels must use the same `sck_pin` and `gain`.
- Calibration is per channel plus one global multiplier:

```cpp
channel = (raw - offset) * scale;
total = sum(channel);
weight = total * global_scale;
```

- Measurement, filtering, Web and display are separate modules.
- The default batch stage has target weight and shovel weight constants, so the display/API can show remaining material and estimated shovels.
- The display module currently ships with a log sink. A HUB75 64x64 sink can be added behind `display::IDisplaySink` without changing measurement or Web code.
- `ESP32-HUB75-MatrixPanel-DMA` is the current candidate for HUB75 output on ESP32-S3.
- The Web module starts an ESP32 access point and exposes:
  - `GET /` - small live page
  - `GET /api/weight` - current raw/filtered values
  - `GET /api/settings` - current calibration
  - `POST /api/settings` - update calibration JSON
- Static Web files live in `www/` and are packed into the `www` SPIFFS partition by CMake.
- Runtime settings do not use that filesystem. Calibration stays in the separate `nvs` partition.

## Configure

Edit:

- `include/config/hardware_config.hpp` - HX711 pins, scales, offsets, sample/filter periods, display size.
- `include/config/network_config.hpp` - access point name and password.
- `include/config/wifi_secrets.example.hpp` - copy to ignored `wifi_secrets.hpp` for local STA credentials.
- `www/index.html`, `www/app.css`, `www/app.js` - static Web UI files.
- `partitions.csv` - flash layout. `nvs` is for settings, `www` is for static files.
- `docs/adafruit-matrixportal-s3.pdf` - local board documentation for Matrix Portal S3.

## Build

```powershell
pio run -e matrix-portal-s3
```

## Upload

```powershell
pio run -e matrix-portal-s3 -t upload
```

The normal PlatformIO upload also uploads the SPIFFS image for the `www`
partition. `platformio.ini` maps PlatformIO's filesystem data directory to
`www/`, and `scripts/upload_www_after_firmware.py` runs the standard `uploadfs`
target after firmware upload.

ESP-IDF builds the same SPIFFS image through:

```cmake
spiffs_create_partition_image(www ../www FLASH_IN_PROJECT)
```

## Calibration POST Example

```json
{
  "globalScale": 1.0,
  "channels": [
    { "index": 0, "offset": 0, "scale": 1.0 },
    { "index": 1, "offset": 0, "scale": 1.0 },
    { "index": 2, "offset": 0, "scale": 1.0 }
  ]
}
```
