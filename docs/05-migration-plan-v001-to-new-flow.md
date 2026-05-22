# План миграции 0.0.1 на новый поток обработки

> Примечание: этот файл является историческим планом миграции. Текущий live API закреплен отдельно в `docs/06-live-api-contract.md`: Web и Controller используют `/api/state.cbor`, JSON `/api/weight` не является текущим live-контрактом.

Цель: сохранить рабочее поведение версии 0.0.1 и менять только отдельные подходы вокруг суммарного виртуального сигнала, hard reject, MA, ролей программ и общего CBOR API.

Проект не переписывать. Работать маленькими изменениями, после каждого шага собирать прошивку и C# проекты, если шаг их затрагивает.

## Текущее состояние

- Сэмплер читает HX711, применяет offsets/scales и публикует `WeightSample`.
- `WeightProcessor` хранит последний результат в `LatestWeightStore`.
- Сейчас `AnomalyPreFilter` делает 5-точечную медиану по каналам и подменяет sample перед фильтрами.
- UDP пишет CSV из `LatestWeightStore`, считает `raw_sum` как `int64_t` из текущих raw.
- Web UI и Controller читают CBOR `/api/state.cbor`; настройки остаются на JSON `/api/settings`, `/api/wifi`, `/api/udp-telemetry`.
- `MixerScale.UdpRecorder` уже в основном только принимает UDP и пишет CSV, но еще парсит пакет и показывает live-статус.
- `MixerScale.Analyzer` сейчас отображает готовые MA из CSV, а не пересчитывает их как основной источник истины.

Главный конфликт с TODO_2: медианный предфильтр сглаживает выброс, а нужен hard reject до MA. Кроме того, `rawSum` должен оставаться логируемым сырым значением, а `cleanSum` должен быть отдельным входом MA.

## Инварианты миграции

- CSV поля `scale_id,seq,ms,rawN,raw_sum,kg_sum,flags` не менять без обновления `docs/04-udp-csv-contract.md`.
- `rawSum` всегда сумма исходных raw-каналов текущей строки, хранить и считать в 64-bit.
- `cleanSum` появляется только после hard reject и не заменяет `rawSum`.
- Сэмпл, отклоненный hard reject, не попадает в MA.
- `UdpRecorder` не пересчитывает фильтры и не принимает решений по данным, только валидирует формат для записи и отображает прием.
- `Analyzer` пересчитывает clean/MA по логам и позволяет менять параметры анализа.
- `Controller` работает по live API и управляет zero/точками, не по CSV-файлам.
- CBOR API должен быть единым live-контрактом для Web и C# Controller. JSON live-слой не добавлять.

## Этап 1. Зафиксировать контракты перед кодом

### Задача 1.1. Расширить CSV контракт терминами rawSum/cleanSum

Файлы:
- `docs/04-udp-csv-contract.md`
- `docs/TODO_2.md`

Что сделать:
- Уточнить, что `raw_sum` является сырой суммой и не фильтруется.
- Описать, что `clean_sum` не обязан быть в базовом CSV 0.0.1, но может добавляться после `flags` или в отдельный CBOR/live контракт.
- Описать флаги hard reject: например `valid`, `invalid|sensor_error`, `invalid|hard_reject`.

Проверка:
- В документе явно написано, что отклоненный сэмпл логируется как raw, но не попадает в MA.
- Формат базовых обязательных полей CSV не изменен.

### Задача 1.2. Завести документ live API контракта

Файлы:
- `docs/06-live-api-contract.md`

Что сделать:
- Описать одну модель live-состояния для CBOR: sequence, timestamp, raw[], rawSum, cleanSum, valid, rejectReason, filters[], zero[], controlPoints[].
- Отдельно отметить, какие поля нужны Web, а какие C# Controller, без разных моделей данных.

Проверка:
- В документе есть один список полей, а не две версии для Web и C#.
- Есть правило однозначности: live-состояние читается через CBOR, без JSON fallback.

## Этап 2. Подготовить доменную модель без изменения поведения

### Задача 2.1. Добавить поля сумм и причины отклонения в WeightSample/WeightState

Файлы:
- `include/domain/weight_types.hpp`
- `src/measurement/load_cell_sampler.cpp`
- `src/telemetry/udp_telemetry.cpp`

Что сделать:
- Добавить в `WeightSample` `int64_t raw_sum`, `int64_t clean_sum`, `bool clean_valid`, `const char* reject_reason` или enum.
- В сэмплере считать `raw_sum` сразу после чтения raw.
- В UDP временно продолжить писать старый CSV, но брать `raw_sum` из `state.sample.raw_sum`.

Проверка:
- `pio run` проходит.
- UDP строка до и после изменения имеет те же обязательные поля.
- `raw_sum` равен сумме `rawN` на тестовой строке.

### Задача 2.2. Убрать прошитые 3 канала из диагностического лога

Файлы:
- `src/measurement/load_cell_sampler.cpp`

Что сделать:
- Не менять формат UI/CSV.
- Только диагностический `ESP_LOGI` сделать безопасным для `kLoadCellCount != 3` или оставить краткую сводку без массива raw.

Проверка:
- `pio run` проходит.
- В бизнес-логике нет новых предположений о трех датчиках.

## Этап 3. Заменить медианный предфильтр на hard reject

### Задача 3.1. Вынести настройки hard reject в конфиг

Файлы:
- `include/config/hardware_config.hpp`
- `docs/03-design-decisions-and-maintenance.md`

Что сделать:
- Добавить `kHardRejectEnabled`.
- Добавить `kHardRejectDeltaRawSum`, начально 500000 или 1000000.
- Зафиксировать, что hard reject сравнивает новый `rawSum` с последним хорошим `cleanSum`.

Проверка:
- `pio run` проходит.
- В документации нет рекомендации median/soft/pending для этой задачи.

### Задача 3.2. Реализовать hard reject по сумме

Файлы:
- `include/processing/weight_processor.hpp`
- `src/processing/weight_processor.cpp`
- `include/domain/weight_types.hpp`

Что сделать:
- Заменить или временно обойти `AnomalyPreFilter`.
- Хранить `last_good_clean_sum` как `int64_t`.
- Если сэмпл невалиден по HX711, не обновлять `last_good_clean_sum`.
- Если `abs(rawSum - lastGoodCleanSum) > hardDelta`, пометить `clean_valid=false`, `reject_reason=hard_reject`, не обновлять MA.
- Если это первый валидный сэмпл, принять его как начальный clean.

Проверка:
- На последовательности `1000, 1010, 9000000, 1020` MA получает `1000, 1010, 1020`, но не получает `9000000`.
- UDP/Web видят seq выброшенного сэмпла, чтобы лог не скрывал сам факт глюка.

### Задача 3.3. Обновить флаги состояния

Файлы:
- `src/telemetry/udp_telemetry.cpp`
- `src/web/web_server.cpp`

Что сделать:
- Для hard reject отдавать `invalid|hard_reject` или `valid|clean_invalid` только после выбора в контракте.
- В `/api/state.cbor` добавить `rawSum`, `cleanSum`, `cleanValid`, `rejectReason`.
- Не убирать старые поля `valid`, `total`, `weight`, `channels`, `filters`.

Проверка:
- Старый Web UI продолжает работать.
- В JSON видна причина hard reject.
- CSV строка содержит понятный flags.

## Этап 4. Перевести MA на cleanSum

### Задача 4.1. Переделать MovingAverageWeightFilter на 64-bit cleanSum

Файлы:
- `include/processing/weight_filters.hpp`
- `src/processing/weight_filters.cpp`

Что сделать:
- Для MA хранить входные суммы как `int64_t`, а результат отдавать как raw/условный вес через существующий `FilterOutput`.
- Если `sample.clean_valid == false`, не писать значение в кольцевой буфер и вернуть `valid=false` для текущего выхода либо последний valid результат после явного решения в контракте.
- Не использовать `float total_values_` для больших raw-сумм.

Проверка:
- На тестовой последовательности с большим rawSum нет переполнения 32-bit.
- Глючный сэмпл не меняет среднее.

### Задача 4.2. Добавить MA30/MA60 без ломки UI

Файлы:
- `include/processing/weight_processor.hpp`
- `include/domain/weight_types.hpp`
- `src/processing/weight_processor.cpp`

Что сделать:
- Увеличить лимит фильтров, если не хватает.
- Добавить `ma_30s` и `ma_60s`.
- Существующие `ma_1s`, `ma_3s`, `ma_5s`, `ma_10s` не переименовывать.

Проверка:
- Web process page по-прежнему показывает старые MA.
- UDP после `flags` добавляет пары для всех MA, а Analyzer их видит как разные окна.

### Задача 4.3. Привязать размер MA к периоду сэмплирования

Файлы:
- `include/processing/weight_processor.hpp`
- `include/config/hardware_config.hpp`

Что сделать:
- Не прошивать `10/30/50/100` руками.
- Завести helper/constexpr пересчета секунд в samples через `kSamplePeriodMs`.

Проверка:
- При `kSamplePeriodMs=100` окна остаются 10/30/50/100/300/600 samples.

## Этап 5. Разделить роли C# программ

### Задача 5.1. Переименовать или документировать роли проектов

Файлы:
- `MixerScaleTools/README.md`
- `MixerScaleTools/MixerScaleTools.slnx`

Что сделать:
- Держать текущие имена проектов в формате `MixerScale.*`.
- Главное: описать роли `UdpRecorder`, `Analyzer`, `Controller`.

Проверка:
- README явно говорит: Recorder пишет, Analyzer пересчитывает, Controller работает live.

### Задача 5.2. Упростить UdpRecorder до записи принятых данных

Файлы:
- `MixerScaleTools/MixerScale.UdpRecorder/Program.cs`
- `MixerScaleTools/MixerScale.UdpRecorder/SensorCsvPacket.cs`
- `MixerScaleTools/MixerScale.UdpRecorder/SessionCsvWriter.cs`

Что сделать:
- Оставить парсинг только как проверку структуры и для имени папки `scale_id`.
- Не добавлять расчет clean/MA/reject в Recorder.
- Не менять текущий live-вывод без необходимости.

Проверка:
- Запуск Recorder принимает старую строку CSV и пишет ее без пересчета.
- Выходной файл содержит исходные поля и extra fields.

### Задача 5.3. Добавить офлайн пересчет clean/MA в Analyzer

Файлы:
- `MixerScaleTools/MixerScale.Analyzer/TelemetryModels.cs`
- `MixerScaleTools/MixerScale.Analyzer/TelemetryCsvReader.cs`
- `MixerScaleTools/MixerScale.Analyzer/MainWindow.cs`

Что сделать:
- После чтения CSV строить производные ряды: rawSum, cleanSum, reject marks, MA1/3/5/10/30/60.
- Не доверять MA из CSV как единственному источнику, а использовать их только для сравнения с прошивкой.
- Порог hardDelta сделать настройкой Analyzer.

Проверка:
- На старом логе без cleanSum Analyzer строит MA заново.
- На логе с одиночным выбросом линия MA не дергается на выброс.

## Этап 6. Live Controller отдельно от Analyzer

### Задача 6.1. Добавить проект Controller без логики управления

Файлы:
- `MixerScaleTools/MixerScaleTools.slnx`
- `MixerScaleTools/MixerScale.Controller/MixerScale.Controller.csproj`
- `MixerScaleTools/MixerScale.Controller/Program.cs`

Что сделать:
- Создать минимальный проект, который читает live state с устройства и показывает sequence/rawSum/cleanSum/MA.
- Пока не переносить UI Analyzer.

Проверка:
- `dotnet build MixerScaleTools/MixerScaleTools.slnx` проходит.
- Controller запускается и не требует CSV.

### Задача 6.2. Перенести live zero после MA в Controller

Файлы:
- `MixerScaleTools/MixerScale.Controller/Program.cs`
- `MixerScaleTools/MixerScale.Controller/ControllerState.cs`
- `docs/06-live-api-contract.md`

Что сделать:
- Zero хранить как значение выбранного MA, а не offsets датчиков.
- Поддержать несколько zero по MA окнам.
- Пока можно хранить локально в памяти, постоянное хранение вынести в отдельный шаг.

Проверка:
- Нажатие zero для MA3 не меняет offsets датчиков.
- Вес относительно zero считается из live MA3.

## Этап 7. Единый CBOR API

### Задача 7.1. Добавить CBOR endpoint как live API

Файлы:
- `src/web/web_server.cpp`
- `include/web/web_server.hpp`
- `docs/06-live-api-contract.md`

Что сделать:
- Добавить `/api/state.cbor`.
- Не добавлять JSON fallback для live state.
- Код сериализации держать рядом с моделью состояния, чтобы прошивка, Web и Controller не расходились по смыслу.

Проверка:
- Web UI ходит в `/api/state.cbor`.
- Endpoint возвращает `application/cbor`.

### Задача 7.2. Подключить CBOR в C# Controller

Файлы:
- `MixerScaleTools/MixerScale.Controller/MixerScale.Controller.csproj`
- `MixerScaleTools/MixerScale.Controller/Program.cs`
- `MixerScaleTools/MixerScale.Controller/LiveStateDto.cs`

Что сделать:
- Добавить `System.Formats.Cbor`.
- Читать тот же live state, который описан в `docs/06-live-api-contract.md`.
- JSON fallback не добавлять в логику Controller.

Проверка:
- Controller читает sequence/rawSum/cleanSum/MA через CBOR.

### Задача 7.3. Подключить CBOR в Web без изменения экранов

Файлы:
- `www/process.js`
- `www/app.js`
- `www/cbor-x.min.js`

Что сделать:
- Добавить локальный `cbor-x.min.js` в SPIFFS.
- Перевести чтение live state на CBOR.
- Не менять визуальное поведение страниц.

Проверка:
- Web страницы открываются из SPIFFS.
- На графиках и таблицах отображаются значения из `/api/state.cbor`.

## Этап 8. Контрольные точки и условные единицы

### Задача 8.1. Добавить модель профиля без UI

Файлы:
- `include/domain/weight_types.hpp`
- `include/settings/settings_store.hpp`
- `src/settings/settings_store.cpp`

Что сделать:
- Добавить до 16 контрольных точек в raw/clean/MA единицах по контракту.
- Добавить до нескольких коэффициентов условных единиц с коротким названием.
- Не менять текущую калибровку каналов.

Проверка:
- Старые настройки NVS не ломают загрузку.
- При schema mismatch профиль сбрасывается отдельно от Wi-Fi и UDP.

### Задача 8.2. Добавить API команд профиля

Файлы:
- `src/web/web_server.cpp`
- `include/web/web_server.hpp`
- `docs/06-live-api-contract.md`

Что сделать:
- Команды: zero по выбранному MA, save point, select point.
- Команды работают через live API, а не через Analyzer CSV.

Проверка:
- Команды не меняют offsets датчиков.
- Состояние профиля видно в live state.

## Риски: слабого агента лучше не пускать

- Настройка `hardDelta` по реальным логам. Это инженерная калибровка, а не механическая правка.
- Выбор семантики flags для hard reject. Неправильный флаг сломает Analyzer/Recorder и будущие графики.
- Одновременный перевод Web и C# на CBOR. Высокий риск получить две несовместимые схемы.
- Изменения в NVS schema `SettingsStore`. Ошибка может сбросить Wi-Fi/UDP/калибровку или сделать устройство неудобным для восстановления.
- Изменения в HX711 чтении, SharedClockBus, ready-timeout и аппаратных пинах. Это не часть миграции rawSum/MA и легко ломает рабочую 0.0.1.
- Перенос текущей кнопки zero offsets. Сейчас она меняет offsets датчиков, а новая логика zero должна быть после MA. Нельзя просто переиспользовать старую команду без смены смысла UI.
- Возврат JSON live endpoint после перевода на CBOR. Это снова создаст два live-контракта и риск расхождения Web/Controller.

## Рекомендуемый порядок для слабого агента

1. Сначала задачи 1.1, 1.2, 2.1.
2. Потом 3.1, 3.2, 3.3.
3. Затем 4.1, 4.2, 4.3.
4. После стабильных логов переходить к 5.1, 5.2, 5.3.
5. Controller и CBOR делать только после того, как rawSum/cleanSum/MA проверены на реальных CSV.

Минимальный набор проверок после каждого этапа:

```powershell
pio run
dotnet build .\MixerScaleTools\MixerScaleTools.slnx
```

Для этапов hard reject и Analyzer дополнительно нужен маленький CSV с последовательностью, где один rawSum отличается от последнего хорошего больше `hardDelta`.
