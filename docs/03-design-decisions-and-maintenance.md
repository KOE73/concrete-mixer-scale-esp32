# Архитектурные решения и сопровождение

## Почему AP+STA, а не только подключение к роутеру

Устройство должно быть доступно в полевых условиях. Если оставить только STA, то при неверном пароле или смене роутера придется перепрошивать плату или подключаться по serial. Поэтому `WifiManager::start()` всегда включает `WIFI_MODE_APSTA`.

AP:

- SSID/пароль берутся из `config::kApSsid` и `config::kApPassword`;
- используется как аварийный и первичный вход в Web UI;
- не зависит от сохраненных STA credentials.

STA:

- берет настройки из `SettingsStore::wifiCredentials()`;
- сохраняется через `SettingsStore::saveWifi()`;
- переподключается через `WifiManager::connect()`.

Локальный файл `include/config/wifi_secrets.hpp` нужен только для удобства первичной прошивки. Он ignored, потому что пароль не должен попадать в Git. После настройки через UI NVS становится главным источником.

## Почему Web UI лежит в SPIFFS

HTML/CSS/JS лежат в `www/`, а не внутри C++ строк.

Причины:

- Web UI можно менять без редактирования C++ обработчиков.
- `WebServer::sendStaticFile()` остается универсальным файловым сервером.
- Раздел `www` в `partitions.csv` отделен от `nvs`, поэтому статические файлы и настройки не конкурируют за один storage.
- Сборка прошивает образ через `spiffs_create_partition_image(www ../www FLASH_IN_PROJECT)`.

Если добавляется новый файл UI, его достаточно положить в `www/`; CMake сам включит его в SPIFFS-образ.

## Почему настройки в NVS, а не в SPIFFS

Калибровка и Wi-Fi credentials пишутся через `SettingsStore` в NVS:

- `SettingsStore::save()` -> namespace `calibration`;
- `SettingsStore::saveWifi()` -> namespace `wifi`.

NVS лучше подходит для маленьких структур настроек: есть commit, namespace, blob и меньше риска случайно смешать настройки с Web-ресурсами.

SPIFFS используется только для статических файлов Web UI.

## Почему есть два драйвера HX711

Текущий выбор задается в `config::kHx711ReadDriver`.

`EspIdfLibSequential`:

- использует `Hx711Reader`;
- внутри вызывает `esp-idf-lib/hx711`;
- читает каналы по очереди;
- проще, стабильнее и подходит для первой версии.

`SharedClockBus`:

- использует `Hx711Bus`;
- требует общий SCK и одинаковый gain;
- читает все DOUT во время одного 24-битного тактирования;
- лучше подходит для 3 HX711, когда нужен почти одномоментный логический сэмпл.

Почему выбор на compile-time через `if constexpr`: для embedded-кода это проще и дешевле, чем виртуальный драйвер в горячем пути. Неиспользуемая ветка не должна попадать в исполняемый путь.

## Почему raw и откалиброванный вес хранятся вместе

`WeightSample` содержит и `raw`, и `channels`, и `weight`.

Это сделано потому что:

- raw нужен для диагностики датчиков и калибровки;
- откалиброванные каналы нужны для понимания вклада каждого датчика;
- итоговый `weight` нужен дисплею и API;
- все эти значения должны относиться к одному `sequence` и `timestamp_us`.

Если raw читать отдельно для Web, можно получить состояние от другого момента времени.

## Почему обработка идет через очередь

`LoadCellSampler::run()` отправляет `WeightSample` в FreeRTOS queue. `WeightProcessor::run()` читает из нее.

Так сэмплер не зависит от времени фильтрации, Web и дисплея. Если очередь переполнена, `LoadCellSampler::run()` выбрасывает старый элемент и кладет новый.

Это правильнее для весов: пользователю нужен свежий вес, а не задержанная история.

## Почему `LatestWeightStore` под mutex

`WeightProcessor` пишет последнее состояние, а `DisplayTask` и `WebServer` читают его из разных задач.

`LatestWeightStore::set()` и `LatestWeightStore::get()` используют mutex и работают копиями. Это проще, чем давать указатели на внутреннее состояние, и безопаснее при одновременном доступе.

## Почему дисплей через `IDisplaySink`

`DisplayTask` знает только про `IDisplaySink`.

Сейчас есть два sink:

- `LogDisplaySink` для отладки без матрицы;
- `Hub75DisplaySink` для реальной HUB75 панели.

`DisplayTask::run()` собирает `DisplayFrame`, а конкретный sink решает, как его показать.

Так можно менять физический дисплей без изменения измерения, обработки и Web.

## Почему HUB75 не получает весь `WeightState`

`Hub75DisplaySink::render()` получает `DisplayFrame`, а не `WeightState`.

Причина: HUB75 должен рисовать кадр, а не знать про raw-каналы, фильтры и batch-логику. Подготовка кадра находится в `DisplayTask::run()`, где уже выбран основной фильтр и посчитан остаток.

Если позже понадобится другой экран, нужно расширять `DisplayFrame`, а не протаскивать в дисплей всю внутреннюю модель.

## Почему WebServer координирует, но не владеет подсистемами

`WebServer` получает ссылки:

- `LatestWeightStore& latest`;
- `SettingsStore& settings`;
- `WebAssets& assets`;
- `WifiManager& wifi`.

Он не создает эти объекты и не хранит глобальные singletons.

Так проще тестировать и сопровождать зависимости: HTTP-слой только преобразует запросы в вызовы методов и ответы JSON.

## Как добавить новый HX711 канал

1. Добавить элемент в `config::kLoadCells`.
2. Задать `name`, `dout_pin`, `sck_pin`, `gain`, `offset`, `scale`, `enabled`.
3. Если используется `SharedClockBus`, убедиться, что все активные каналы имеют один `sck_pin` и один `gain`.
4. Пересобрать прошивку.

Остальной код должен взять новый размер через `config::kLoadCellCount`. Если где-то придется менять ручной индекс, это сигнал, что появилась лишняя привязка к количеству каналов.

## Как менять частоту измерения и сглаживание

В `hardware_config.hpp`:

- `kSamplePeriodMs` задает период чтения HX711;
- `kHx711ReadyTimeoutMs` задает ожидание готовности;
- `kMovingAverageWindow` задает окно moving average;
- `kExponentialAlpha` задает скорость экспоненциального фильтра.

Практическое правило: сначала добиться стабильного raw и корректной калибровки, потом менять фильтры. Иначе фильтр будет скрывать аппаратную или калибровочную проблему.

## Как менять Web UI

Править:

- `www/index.html`;
- `www/app.css`;
- `www/app.js`.

API лежит в `WebServer`:

- вес: `sendWeight()`;
- калибровка: `sendSettings()` / `updateSettings()`;
- Wi-Fi: `sendWifi()` / `updateWifi()`.

Если UI требует новые данные, сначала решить, кто ими владеет. Например:

- последнее измерение -> `LatestWeightStore`;
- постоянная настройка -> `SettingsStore`;
- сетевой статус -> `WifiManager`;
- статический файл -> `WebAssets`.

## Как менять Wi-Fi поведение

AP параметры менять в `network_config.hpp`.

STA поведение менять в `WifiManager`:

- старт AP+STA: `start()`;
- применение новых credentials: `connect()`;
- обработка disconnect/got IP: `handleEvent()`;
- публичный статус: `status()`.

Не стоит переносить переподключение в WebServer. WebServer видит только HTTP-запросы, а Wi-Fi события приходят независимо от браузера.

## Как менять partitions

Текущий `partitions.csv` рассчитан на 8 MB flash Matrix Portal S3:

- `nvs` хранит маленькие настройки;
- `phy_init` хранит RF init данные;
- `factory` хранит приложение;
- `www` хранит SPIFFS с Web UI.

Если увеличится приложение, сначала смотреть размер `factory`. Если увеличится Web UI, смотреть `www`. NVS не должен использоваться для HTML/CSS/JS, а SPIFFS не должен использоваться для калибровки и паролей.

## Типовые точки входа при отладке

- Нет веса: `LoadCellSampler::readSample()`, затем `LoadCellReader::waitAllReady()`.
- Один канал молчит: `LoadCellReader::isReady(index)` и `config::kLoadCells[index]`.
- Нужно проверить общий SCK: `Hx711Bus::validateConfiguration()` и `Hx711Bus::readRaw()`.
- Вес дергается: `WeightProcessor::process()` и фильтры в `weight_filters.cpp`.
- Дисплей не показывает: `DisplayTask::run()` и `Hub75DisplaySink::begin()`.
- Web UI не открывается: `WifiManager::start()`, `WebAssets::mount()`, `WebServer::start()`.
- STA не подключается: `WebServer::updateWifi()`, `SettingsStore::saveWifi()`, `WifiManager::connect()`, `WifiManager::handleEvent()`.

## Проверка после изменений

Минимальная проверка:

```powershell
& 'C:\Users\koe\.platformio\penv\Scripts\pio.exe' run -e matrix-portal-s3
```

Что смотреть в выводе:

- board должен быть `Adafruit MatrixPortal ESP32-S3`;
- hardware должен показывать `8MB Flash`;
- не должно быть предупреждения `Expected 8MB, found 2MB`;
- сборка должна завершаться `SUCCESS`.

Для изменений Web UI важно помнить: файлы из `www/` попадут на устройство только при прошивке SPIFFS partition image, который создается через CMake.
