# MixerScaleTools

Отдельное C#-решение для приёма и первичного анализа сырых данных весов.

## Формат UDP CSV

Один UDP-пакет равен одной CSV-строке:

```csv
scale_id,seq,ms,raw_sum,kg_sum,flags
```

Центральный контракт полей, причины и последствия описаны в `..\docs\04-udp-csv-contract.md`.

## UDP recorder

Проект: `MixerScale.UdpRecorder`.

Запуск из папки репозитория:

```powershell
dotnet run --project .\MixerScaleTools\MixerScale.UdpRecorder\MixerScale.UdpRecorder.csproj
```

По умолчанию слушает UDP `4222` и пишет в `C:\ScaleData`.
Первое поле CSV (`scale_id`) выбирает подпапку, поэтому файлы раскладываются так:

```text
C:\ScaleData\1\scale-raw-YYYYMMDD-HHMMSS.csv
C:\ScaleData\2\scale-raw-YYYYMMDD-HHMMSS.csv
```

Файл создаётся один раз на запуск, имя строится от момента запуска:

```text
scale-raw-YYYYMMDD-HHMMSS.csv
```

Старые файлы не стираются. Если имя занято, добавляется индекс `-001`, `-002` и так далее. При явном указании файла через `--file` запись идёт append-режимом.

Стандартные настройки лежат в `MixerScale.UdpRecorder/appsettings.json`:

```json
{
    "port": 4222,
    "dataDirectory": "C:\\ScaleData"
}
```

Параметры:

```powershell
dotnet run --project .\MixerScaleTools\MixerScale.UdpRecorder\MixerScale.UdpRecorder.csproj -- --port 4222 --data C:\ScaleData
dotnet run --project .\MixerScaleTools\MixerScale.UdpRecorder\MixerScale.UdpRecorder.csproj -- --file C:\ScaleData\manual.csv
```

## Analyzer

Проект: `MixerScale.Analyzer`.

```powershell
dotnet run --project .\MixerScaleTools\MixerScale.Analyzer\MixerScale.Analyzer.csproj
```

Стандартные настройки лежат в `MixerScale.Analyzer/appsettings.json`:

```json
{
    "dataDirectory": "C:\\ScaleData"
}
```

Слева показывает браузер этой папки: сначала список подпапок весов (`1`, `2`, `3`), ниже CSV-файлы выбранных весов с размером. При старте автоматически выбирается папка `1`.
Выбор файла в списке загружает его на график. Кнопка `Сменить папку` временно переключает текущую корневую папку в запущенной программе.
Переключатель `Автоподгрузка` отслеживает выбранную подпапку и перечитывает выбранный файл с задержкой после пачки изменений, чтобы запись 10 раз в секунду не запускала 10 перерисовок подряд.

Сейчас показывает:

- график `raw_sum`;
- график `kg_sum` на правой оси;
- диапазон `seq` и `ms`;
- число raw-каналов;
- найденные пропуски `seq`.

## Controller

Проект: `MixerScale.Controller`.

```powershell
dotnet run --project .\MixerScaleTools\MixerScale.Controller\MixerScale.Controller.csproj
```

Стандартные настройки лежат в `MixerScale.Controller/appsettings.json`:

```json
{
    "deviceBaseUrl": "http://192.168.20.41",
    "pollIntervalMs": 1000,
    "requestTimeoutMs": 5000
}
```

Controller работает с live API устройства: основное live-состояние читает из `/api/state.cbor`, а настройки и статусы окружения читает из `/api/wifi`, `/api/settings`, `/api/udp-telemetry`.

## Отладка

Для Visual Studio добавлена конфигурация `Recorder + Analyzer` в `MixerScaleTools.slnLaunch`.
Она запускает проекты одновременно: UDP-рекордер, Avalonia-анализатор и Controller.
