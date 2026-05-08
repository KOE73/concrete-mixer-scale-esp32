# Взаимодействие модулей и сценарии работы

## Общая схема данных

```text
HX711 -> LoadCellReader -> LoadCellSampler -> FreeRTOS queue
      -> WeightProcessor -> LatestWeightStore
      -> DisplayTask -> IDisplaySink -> Hub75DisplaySink
      -> WebServer -> /api/weight -> www/app.js
```

Настройки идут отдельным путем:

```text
Web UI -> WebServer::updateSettings() -> SettingsStore::save()
Web UI -> WebServer::updateWifi() -> SettingsStore::saveWifi() -> WifiManager::connect()
```

Так сделано намеренно. Измерение веса не должно зависеть от HTTP-запросов, SPIFFS или состояния Wi-Fi. Web и дисплей читают последнее опубликованное состояние, а не вмешиваются в цикл HX711.

## Старт прошивки

`app_main()` в `src/main.cpp` создает статические объекты, чтобы они жили весь срок работы прошивки:

- `SettingsStore settings`
- `LoadCellSampler sampler`
- `WeightProcessor processor`
- `DisplayTask display`
- `WebAssets web_assets`
- `WifiManager wifi`
- `WebServer web`

Дальше `app_main()` запускает методы:

1. `settings.load()`
2. `sampler.initialize()`
3. `processor.start()`
4. `sampler.start()`
5. `display.start()`
6. `web_assets.mount()`
7. `wifi.start()`
8. `web.start()`

Почему `processor.start()` идет до `sampler.start()`: как только сэмплер начнет отправлять `WeightSample` в очередь, уже должен быть потребитель. Очередь небольшая, поэтому лишнее ожидание на старте не нужно.

Почему `wifi.start()` идет перед `web.start()`: HTTP-сервер может стартовать и без подключения STA, но практический смысл Web появляется после сети. AP при этом всегда остается активным.

## Сценарий чтения веса

### Уровень 1: периодический сэмплер

`LoadCellSampler::run()` работает в отдельной FreeRTOS задаче.

На каждом цикле:

1. вызывает `readSample()`;
2. отправляет результат в `output_queue_`;
3. если очередь заполнена, выбрасывает старый сэмпл и кладет новый;
4. ждет `config::kSamplePeriodMs`.

Почему старый сэмпл выбрасывается: для весов важнее актуальное состояние, чем обработка всей истории. Если процессор не успел, очередь не должна копить устаревшие измерения.

### Уровень 2: подготовка одного `WeightSample`

`LoadCellSampler::readSample()`:

1. увеличивает `sequence`;
2. берет текущую калибровку через `SettingsStore::calibration()`;
3. вызывает `LoadCellReader::waitAllReady(config::kHx711ReadyTimeoutMs)`;
4. фиксирует `timestamp_us`;
5. вызывает `LoadCellReader::readRaw(sample.raw)`;
6. для каждого активного канала применяет:

```text
channel_weight = (raw - offset) * scale
```

7. суммирует каналы в `sample.total`;
8. применяет `calibration.global_scale` в `sample.weight`.

Почему timestamp ставится после готовности HX711: так время ближе к моменту физического чтения, а не к началу ожидания готовности.

### Уровень 3: выбранный драйвер HX711

`LoadCellReader::readRaw()` выбирает путь на этапе компиляции:

- `config::Hx711ReadDriver::EspIdfLibSequential` -> `readRawSequential()`
- `config::Hx711ReadDriver::SharedClockBus` -> `Hx711Bus::readRaw()`

Последовательный режим:

```text
LoadCellReader::readRawSequential()
  -> Hx711Reader::readRaw(channel 0)
  -> Hx711Reader::readRaw(channel 1)
  -> Hx711Reader::readRaw(channel 2)
```

Общий SCK:

```text
LoadCellReader::readRaw()
  -> Hx711Bus::readRaw()
     -> 24 SCK импульса
     -> чтение всех DOUT на каждом бите
     -> дополнительные gain импульсы
```

Почему есть два режима: готовая библиотека проще и надежнее для запуска, но общий SCK нужен для более честного логического сэмпла с 3 HX711.

## Сценарий обработки и публикации веса

`WeightProcessor::run()` блокируется на `xQueueReceive()`. Когда приходит `WeightSample`, он вызывает `process(sample)`.

`WeightProcessor::process()`:

1. копирует исходный `sample` в `WeightState::sample`;
2. последовательно вызывает `IWeightFilter::apply()` у фильтров;
3. складывает результаты в `WeightState::filters`;
4. возвращает `WeightState`.

`WeightProcessor::run()` передает результат в `LatestWeightStore::set()`.

Почему фильтры живут здесь, а не в сэмплере: сэмплер должен заниматься физикой и калибровкой, а не политикой отображения. Это позволяет добавлять фильтры без риска сломать тайминг чтения HX711.

## Сценарий отображения на HUB75

`DisplayTask::run()`:

1. читает `LatestWeightStore::get()`;
2. выбирает основной результат фильтра:
   - если есть moving average, берет `state.filters[1]`;
   - иначе берет `state.filters[0]`;
3. собирает `DisplayFrame`;
4. вызывает `sink_.render(frame)`;
5. ждет `config::kDisplayRefreshPeriodMs`.

Если выбран `DisplayDriver::Hub75`, в `main.cpp` создается `Hub75DisplaySink`.

`Hub75DisplaySink::begin()`:

- собирает `HUB75_I2S_CFG::i2s_pins` из `config::kHub75*Pin`;
- создает `MatrixPanel_I2S_DMA`;
- вызывает `begin()`;
- задает яркость через `setBrightness8()`;
- очищает экран.

`Hub75DisplaySink::render()`:

- при невалидном сэмпле рисует нейтральную рамку;
- при нормальном весе рисует зеленый прогресс;
- при перевесе рисует красный прогресс.

Почему дисплей получает `DisplayFrame`, а не `WeightState`: панель не должна знать о raw-каналах, фильтрах и HTTP API. Ей нужен только подготовленный кадр: стадия, текущий вес, цель, остаток, валидность.

## Сценарий Web UI

Статические файлы лежат в `www/`:

- `index.html`
- `app.css`
- `app.js`

При сборке `src/CMakeLists.txt` создает SPIFFS-образ для partition `www`. На устройстве `WebAssets::mount()` монтирует его в `/www`.

`WebServer::sendStaticFile()`:

1. превращает `/` в `/index.html`;
2. запрещает `..` в пути;
3. открывает файл из `assets_.basePath()`;
4. отдает его chunk-ами.

Почему HTML/CSS/JS вынесены из C++: Web UI легче менять, смотреть и расширять. C++ остается HTTP/API слоем, а не контейнером строк с HTML.

## Сценарий чтения веса через Web

`www/app.js` каждые 500 мс вызывает:

```text
GET /api/weight
```

`WebServer::weightHandler()` перенаправляет запрос в `sendWeight()`.

`WebServer::sendWeight()`:

1. читает `LatestWeightStore::get()`;
2. кладет в JSON `sequence`, `timestampUs`, `valid`, `total`, `weight`;
3. добавляет цель партии: stage, target, remaining, remainingShovels;
4. добавляет массив каналов с raw и весом;
5. добавляет массив фильтров.

Почему Web читает `LatestWeightStore`, а не очередь: очередь принадлежит процессору. Если Web начнет читать очередь, он будет отбирать сэмплы у обработки. `LatestWeightStore` решает это одной копией последнего состояния.

## Сценарий изменения калибровки

Web отправляет:

```text
POST /api/settings
```

`WebServer::updateSettings()`:

1. читает JSON body;
2. берет текущую калибровку через `SettingsStore::calibration()`;
3. применяет только переданные поля;
4. вызывает `SettingsStore::save(calibration)`;
5. возвращает обновленные настройки через `sendSettings()`.

`SettingsStore::save()` пишет blob в NVS namespace `calibration`, затем обновляет RAM-копию.

Почему RAM-копия обновляется только после успешного commit: если NVS запись упала, прошивка не должна показывать пользователю состояние, которое исчезнет после перезагрузки.

## Сценарий подключения к существующей Wi-Fi сети

На старте `WifiManager::start()`:

1. вызывает `esp_netif_init()`;
2. создает AP netif через `esp_netif_create_default_wifi_ap()`;
3. создает STA netif через `esp_netif_create_default_wifi_sta()`;
4. регистрирует обработчики `WIFI_EVENT` и `IP_EVENT_STA_GOT_IP`;
5. ставит `WIFI_MODE_APSTA`;
6. настраивает AP `MixerScale`;
7. запускает Wi-Fi через `esp_wifi_start()`;
8. берет `SettingsStore::wifiCredentials()`;
9. если credentials заполнены, вызывает `connect(credentials)`.

Web UI каждые 3 секунды вызывает:

```text
GET /api/wifi
```

`WebServer::sendWifi()` отдает:

- AP started/ssid;
- STA configured/connected/ssid/ip;
- флаг `hasPassword`, но не сам пароль.

При сохранении формы:

```text
POST /api/wifi
```

`WebServer::updateWifi()`:

1. парсит `ssid` и `password`;
2. вызывает `SettingsStore::saveWifi(credentials)`;
3. вызывает `WifiManager::connect(settings_.wifiCredentials())`;
4. возвращает свежий статус через `sendWifi()`.

Почему AP не выключается после подключения STA: это сервисный вход. Если пароль неверный, роутер сменился или сеть пропала, устройство все равно доступно через `MixerScale`.

## Сценарий переподключения STA

`WifiManager::handleEvent()` получает события:

- `WIFI_EVENT_STA_DISCONNECTED`
- `IP_EVENT_STA_GOT_IP`

При disconnect:

1. сбрасывает `sta_connected`;
2. очищает `sta_ip`;
3. берет сохраненные credentials из `SettingsStore`;
4. если они есть, вызывает `esp_wifi_connect()`.

При got IP:

1. ставит `sta_connected = true`;
2. сохраняет IP в `WifiStatus::sta_ip`;
3. Web UI начинает показывать адрес.

Почему reconnect здесь, а не в Web: сеть может пропасть без HTTP-запросов. Переподключение должно быть реакцией сетевого менеджера на системное событие.
