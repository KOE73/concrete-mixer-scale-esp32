# ScaleDataTools

Отдельное C#-решение для приёма и первичного анализа сырых данных весов.

## Формат UDP CSV

Один UDP-пакет равен одной CSV-строке:

```csv
scale_id,seq,ms,raw1,raw2,raw3,raw_sum,kg_sum,flags
```

Центральный контракт полей, причины и последствия описаны в `..\docs\04-udp-csv-contract.md`.

## UDP recorder

Проект: `ScaleData.UdpRecorder`.

Запуск из папки репозитория:

```powershell
dotnet run --project .\ScaleDataTools\ScaleData.UdpRecorder\ScaleData.UdpRecorder.csproj
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

Стандартные настройки лежат в `ScaleData.UdpRecorder/appsettings.json`:

```json
{
    "port": 4222,
    "dataDirectory": "C:\\ScaleData"
}
```

Параметры:

```powershell
dotnet run --project .\ScaleDataTools\ScaleData.UdpRecorder\ScaleData.UdpRecorder.csproj -- --port 4222 --data C:\ScaleData
dotnet run --project .\ScaleDataTools\ScaleData.UdpRecorder\ScaleData.UdpRecorder.csproj -- --file C:\ScaleData\manual.csv
```

## Analyzer

Проект: `ScaleData.Analyzer`.

```powershell
dotnet run --project .\ScaleDataTools\ScaleData.Analyzer\ScaleData.Analyzer.csproj
```

Стандартные настройки лежат в `ScaleData.Analyzer/appsettings.json`:

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

## Отладка

Для Visual Studio добавлена конфигурация `Recorder + Analyzer` в `ScaleDataTools.slnLaunch`.
Она запускает оба проекта одновременно: UDP-рекордер и Avalonia-анализатор.
