# Live API contract

Live-состояние устройства отдается только через CBOR:

```http
GET /api/state.cbor
Accept: application/cbor
Content-Type: application/cbor
```

Web UI и `MixerScale.Controller` читают один и тот же CBOR payload. JSON live endpoint `/api/weight` не является частью текущего контракта.

Настроечные endpoints остаются JSON:

- `GET /api/settings`, `POST /api/settings`
- `GET /api/wifi`, `POST /api/wifi`
- `GET /api/udp-telemetry`, `POST /api/udp-telemetry`

## State

Top-level CBOR map содержит 12 полей:

| Поле | Тип | Описание |
| --- | --- | --- |
| `sequence` | uint64 | Номер сэмпла из прошивки. |
| `timestampUs` | int64 | Время ESP32 в микросекундах. |
| `valid` | bool | Физический сэмпл прочитан без ошибки HX711. |
| `cleanValid` | bool | Сэмпл принят hard reject фильтром и попал в clean/MA поток. |
| `rejectReason` | text | Пустая строка для принятого сэмпла, иначе `sensor_error` или `hard_reject`. |
| `rawSum` | int64 | Сырая сумма всех физических HX711 входов. Не фильтруется. |
| `cleanSum` | int64 | Последняя принятая сумма после hard reject. Отклоненный сэмпл не обновляет это значение. |
| `total` | float64 | Значение виртуального датчика после `sumOffset/sumScale`. |
| `weight` | float64 | Сейчас равно `total`; оставлено как рабочее поле веса. |
| `diagnosticPartialRead` | bool | Включен ли диагностический режим частичного чтения готовых HX711. |
| `target` | map | Состояние текущей цели замеса. |
| `ma` | array | MA и raw-выходы виртуального датчика. |

## Target

| Поле | Тип | Описание |
| --- | --- | --- |
| `stage` | text | Имя текущего этапа. |
| `weight` | float64 | Целевой вес этапа. |
| `remaining` | float64 | Остаток до цели. |
| `remainingShovels` | float64 | Остаток в условных лопатах. |

## MA Output

| Поле | Тип | Описание |
| --- | --- | --- |
| `name` | text | `raw`, `ma_1s`, `ma_3s`, `ma_5s`, `ma_10s`, `ma_30s`, `ma_60s`, `exponential`. |
| `valid` | bool | У MA есть значение. При hard reject MA не обновляется. |
| `rawSum` | int64 | Значение MA в raw/clean-sum единицах. |
| `total` | float64 | Значение MA после текущего scale. |
| `weight` | float64 | Рабочее значение MA. |

## Settings JSON

```json
{
  "sumOffset": 0,
  "sumScale": 1.0,
  "units": [
    {
      "name": "kg",
      "rawPerUnit": 1000.0
    }
  ],
  "setpoints": [
    {
      "name": "water60",
      "rawValue": 182715
    }
  ]
}
```

`units` — массив условных единиц, максимум 8 записей. `name` хранится до 5
символов и содержит только английские буквы, `rawPerUnit` задает, сколько
raw/MA единиц приходится на одну условную единицу.

`setpoints` — массив уставок, максимум 16 записей. `rawValue` хранится в
raw/MA единицах, чтобы Web и `MixerScale.Controller` могли сравнивать любую MA
с сохраненной контрольной отметкой без пересчета на ESP32.

## Правила

- Web и `MixerScale.Controller` используют только `/api/state.cbor` для live-состояния.
- В live state нет массива каналов HX711.
- Калибровка одна на сумму: `weight = (rawSum - sumOffset) * sumScale`.
- `rawSum` нужен для диагностики и логов. По нему видны цифровые выбросы.
- `cleanSum` и MA нужны для управления. Hard reject не должен попадать в MA.
- Суммы хранятся и передаются как 64-bit значения.
