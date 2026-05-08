# Назначение и карта модулей

## Кратко

Прошивка предназначена для весов бетонного смесителя на ESP32-S3 Matrix Portal. Она читает несколько тензодатчиков через HX711, собирает один логический замер, калибрует каналы, фильтрует вес, показывает состояние на HUB75-матрице и отдает Web UI/API.

Главная идея архитектуры: физическое чтение, обработка, отображение, Web и постоянные настройки разделены. Это сделано не ради абстракций, а чтобы можно было менять рискованные части по отдельности: например, заменить способ чтения HX711 на общий SCK без переписывания Web UI, или заменить HUB75 на логовый дисплей без изменения расчета веса.

## Точка входа

`app_main()` в `src/main.cpp` собирает все долгоживущие объекты и запускает их в правильном порядке:

1. `initializeNvs()` поднимает NVS, потому что калибровка и Wi-Fi настройки нужны до старта измерений и сети.
2. Создается очередь `sample_queue`, через которую измеритель отправляет сырые логические сэмплы процессору.
3. `SettingsStore::load()` загружает калибровку и STA Wi-Fi настройки.
4. `LoadCellSampler::initialize()` инициализирует выбранный драйвер HX711.
5. `WeightProcessor::start()` запускает обработку очереди.
6. `LoadCellSampler::start()` запускает периодическое чтение датчиков.
7. `DisplayTask::start()` запускает индикацию.
8. `WebAssets::mount()` монтирует SPIFFS с HTML/CSS/JS.
9. `WifiManager::start()` включает AP+STA сеть.
10. `WebServer::start()` публикует HTTP API и статические файлы.

Такой порядок важен: настройки должны быть загружены до сэмплера, SPIFFS должен быть смонтирован до Web UI, а Wi-Fi должен быть поднят до ожидания внешних HTTP-запросов.

## Доменные типы

`include/domain/weight_types.hpp`

- `CalibrationState` хранит offset/scale по каждому HX711 каналу и общий множитель. Его используют `SettingsStore`, `LoadCellSampler` и Web API, поэтому это простой DTO без логики.
- `WeightSample` описывает один логический замер: `raw`, откалиброванные веса каналов, `total`, итоговый `weight`, `timestamp_us`, `sequence`, `valid`.
- `FilterOutput` описывает результат одного фильтра. Несколько фильтров могут работать параллельно над одним сэмплом.
- `WeightState` объединяет последний физический сэмпл и массив результатов фильтров. Это состояние читает и дисплей, и Web.

Почему так: один общий контракт снижает связность. Web не знает, как читается HX711, а дисплей не знает, какие фильтры существуют внутри процессора.

## Конфигурация

`include/config/hardware_config.hpp`

- `Hx711ChannelConfig` описывает физический канал: имя, DOUT, SCK, gain, offset, scale, enabled.
- `kLoadCells` задает список тензодатчиков. Сейчас массив на 3 канала, но остальной код берет размер через `kLoadCellCount`.
- `Hx711ReadDriver` выбирает способ чтения: `EspIdfLibSequential` или `SharedClockBus`.
- `DisplayDriver` выбирает приемник индикации: `Hub75` или `Log`.
- HUB75 пины Matrix Portal S3 собраны рядом с параметрами матрицы.

Почему так: аппаратные решения должны быть в одном месте. Если пин или тип драйвера спрятать в модуле, потом легко получить несовместимую схему, где сэмплер думает одно, а низкоуровневый драйвер делает другое.

`include/config/network_config.hpp`

- AP всегда включен с `kApSsid`/`kApPassword`.
- STA берет первичный SSID/пароль из ignored-файла `include/config/wifi_secrets.hpp`.
- Если Web UI сохранил Wi-Fi в NVS, NVS имеет приоритет над прошитым секретом.

Почему так: AP остается аварийным входом в устройство, а STA дает нормальную работу в существующей сети.

## Измерение

`include/measurement/load_cell_reader.hpp`, `src/measurement/load_cell_reader.cpp`

- `LoadCellReader::initialize()` выбирает реализацию чтения через `if constexpr` по `config::kHx711ReadDriver`.
- `LoadCellReader::waitAllReady()` ждет готовность всех активных каналов.
- `LoadCellReader::readRaw()` возвращает массив raw-значений.

Почему так: сэмплеру нужен один интерфейс, а не знание о двух разных способах чтения HX711.

`include/platform/hx711_reader.hpp`, `src/platform/hx711_reader.cpp`

- `Hx711Reader` оборачивает `esp-idf-lib/hx711`.
- Читает один HX711 канал.

Почему так: готовая библиотека нормальна для первой версии и меньше рискует ошибками протокола.

`include/platform/hx711_bus.hpp`, `src/platform/hx711_bus.cpp`

- `Hx711Bus::readRaw()` тактирует общий SCK и читает все DOUT в одном 24-битном цикле.
- `Hx711Bus::validateConfiguration()` требует один общий SCK и одинаковый gain у всех активных каналов.

Почему так: для 3 HX711 желательно получить почти одномоментный логический сэмпл. Последовательная библиотека читает каналы по очереди, а общий SCK уменьшает временной разброс.

`include/measurement/load_cell_sampler.hpp`, `src/measurement/load_cell_sampler.cpp`

- `LoadCellSampler::run()` периодически вызывает `readSample()` и отправляет `WeightSample` в очередь.
- `LoadCellSampler::readSample()` берет калибровку из `SettingsStore::calibration()`, ждет готовность HX711, читает raw, применяет offset/scale/global scale.

Почему так: калибровка применяется сразу после raw-чтения, чтобы дальше по системе шел уже нормализованный вес.

## Обработка

`include/processing/weight_filters.hpp`, `src/processing/weight_filters.cpp`

- `RawWeightFilter` публикует вес без сглаживания.
- `MovingAverageWeightFilter` сглаживает окно последних значений.
- `ExponentialWeightFilter` дает быстрый сглаженный отклик.

`include/processing/weight_processor.hpp`, `src/processing/weight_processor.cpp`

- `WeightProcessor::run()` читает `WeightSample` из очереди.
- `WeightProcessor::process()` применяет все фильтры и формирует `WeightState`.
- `LatestWeightStore::set()` публикует последнее состояние под mutex.
- `LatestWeightStore::get()` отдает копию дисплею и Web.

Почему так: измерение не должно ждать Web или дисплей. Очередь отделяет realtime-чтение от потребителей, а `LatestWeightStore` дает быстрый read-only доступ к последнему состоянию.

## Индикация

`include/display/display.hpp`, `src/display/display.cpp`

- `IDisplaySink` задает интерфейс приемника вывода.
- `DisplayTask::run()` читает `LatestWeightStore::get()`, выбирает основной фильтр и строит `DisplayFrame`.
- `LogDisplaySink` пишет состояние в лог.

`src/display/hub75_display.cpp`

- `Hub75DisplaySink::begin()` инициализирует `ESP32-HUB75-MatrixPanel-DMA`.
- `Hub75DisplaySink::render()` рисует состояние партии на HUB75.

Почему так: задача дисплея знает о весе и партии, но не знает о деталях конкретной панели. Конкретная панель живет в `Hub75DisplaySink`.

## Web, Wi-Fi и файлы

`include/storage/web_assets.hpp`, `src/storage/web_assets.cpp`

- `WebAssets::mount()` монтирует SPIFFS раздел `www`.
- HTML/CSS/JS лежат в `www/` и прошиваются как образ файловой системы.

`include/web/wifi_manager.hpp`, `src/web/wifi_manager.cpp`

- `WifiManager::start()` включает `WIFI_MODE_APSTA`, поднимает AP и при наличии настроек запускает STA.
- `WifiManager::connect()` применяет новые STA credentials и вызывает `esp_wifi_connect()`.
- `WifiManager::status()` отдает состояние AP/STA для Web.

`include/web/web_server.hpp`, `src/web/web_server.cpp`

- `WebServer::sendWeight()` отдает `/api/weight`.
- `WebServer::sendSettings()` и `updateSettings()` читают/пишут калибровку.
- `WebServer::sendWifi()` и `updateWifi()` читают/пишут Wi-Fi настройки и вызывают `WifiManager::connect()`.
- `WebServer::sendStaticFile()` раздает файлы из SPIFFS.

Почему так: WebServer является границей HTTP, но не владеет ни Wi-Fi стеком, ни NVS напрямую. Он координирует запросы между `SettingsStore`, `WifiManager`, `LatestWeightStore` и `WebAssets`.
