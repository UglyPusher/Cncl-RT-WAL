# Технология работы разработчика с каналами STAM v1

*Источник: chanel_structure.md (контракты) + working_technology.md (реестр) + fixed.md (FIXED-CHAN-001..007)*

---

## 1. Роль каналов в системе

Канал — единственный законный путь передачи данных между задачами.
Прямое обращение к данным другой задачи запрещено.

Разработчик описывает каналы **один раз** в функции `init()`, до `seal()`.
После `seal()` топология каналов заморожена: изменение, добавление, удаление — невозможны.

Два типа каналов:

| Тип | Когда использовать |
|-----|--------------------|
| `EventChannel<T, Capacity, DropPolicy>` | Поток событий: каждый элемент должен быть обработан ровно одним reader |
| `StateChannel<T, N>` | Состояние: N задач читают последнее опубликованное значение |

---

## 2. Шаг 1 — Объявить порты в payload

Каждая задача объявляет свои каналы **в payload** через public-поля типа `InPort<T>` / `OutPort<T>`.

```cpp
// Задача-sensor: публикует температуру и давление
struct SensorPayload {
    using rt_class = stam::model::rt_safe_tag;

    OutPort<float>   temp_out;      // writer → StateChannel<float, N>
    OutPort<float>   pressure_out;  // writer → StateChannel<float, N>

    void step(tick_t now) noexcept {
        temp_out.publish(read_temp_sensor());
        pressure_out.publish(read_pressure_sensor());
    }
};

// Задача-controller: читает состояние, пишет команды
struct CtrlPayload {
    using rt_class = stam::model::rt_safe_tag;

    InPort<float>    temp_in;       // reader ← StateChannel<float, N>
    InPort<float>    pressure_in;   // reader ← StateChannel<float, N>
    OutPort<Command> cmd_out;       // writer → EventChannel<Command, 8, LastLoss>

    void step(tick_t now) noexcept {
        float temp{};
        if (temp_in.try_read(temp)) {
            // data available
        }
        // false = no-data → штатный результат, не ошибка; retry на следующем тике
    }
};

// Задача-logger: читает команды
struct LoggerPayload {
    InPort<Command> cmd_in;         // reader ← EventChannel<Command, 8, LastLoss>

    void step(tick_t now) noexcept {
        Command cmd{};
        while (cmd_in.pop(cmd)) {
            log(cmd);
        }
    }
};
```

**Правила объявления портов:**
- Порт — public-поле payload, имя поля используется при `assign_port`.
- `OutPort<T>` — writer-сторона (только `publish()` или `push()`).
- `InPort<T>` — reader-сторона (только `try_read()` или `pop()`).
- Портов в payload может быть произвольное количество.
- Порты non-copyable; lifetime порта = lifetime payload.

---

## 3. Шаг 2 — Создать каналы в init()

Каналы создаются как объекты со временем жизни не короче runtime (статические глобальные или file-scope объекты; не stack-локальные в `init()`).

```cpp
// EventChannel: SPSC, FIFO, LastLoss
EventChannel<Command, /*Capacity=*/8, LastLoss>  ch_cmd;

// StateChannel: SPMC, latest-wins; N = точное число readers
StateChannel<float, /*N=*/1>  ch_temp;      // 1 reader (ctrl)
StateChannel<float, /*N=*/1>  ch_pressure;  // 1 reader (ctrl)
```

**Правила выбора типа:**
- `EventChannel` — если нужна очередь: порядок важен, каждый элемент обрабатывается один раз.
- `StateChannel` — если нужно состояние: после первой публикации reader читает последнее опубликованное значение; пропуск промежуточных обновлений допустим. До первой публикации и на SMP без Condition B — возможен `false/no-data`.

**Правила задания N для StateChannel:**
- N = **точное** число задач, которые будут читать этот канал.
- N задаётся на этапе компиляции и фиксируется в типе.
- Если N не совпадёт с числом привязанных readers → `SealError`.
- N ≤ 30 (ограничение примитива: K = N+2 ≤ 32).

**Правила задания Capacity для EventChannel:**
- Capacity — размер кольцевого буфера. Полезная ёмкость = Capacity − 1.
- Должна быть степенью двойки, ≥ 2.

---

## 4. Шаг 3 — Привязать каналы к портам задач

Привязка задаёт топологию: кто пишет в канал, кто читает.

```cpp
// registry.assign_port(task_name, payload_port_name, channel, side)
// side: writer | reader
registry.assign_port("SNSR", "temp_out",     ch_temp,     writer);
registry.assign_port("CTRL", "temp_in",      ch_temp,     reader);

registry.assign_port("SNSR", "pressure_out", ch_pressure, writer);
registry.assign_port("CTRL", "pressure_in",  ch_pressure, reader);

registry.assign_port("CTRL", "cmd_out",      ch_cmd,      writer);
registry.assign_port("LOGR", "cmd_in",       ch_cmd,      reader);
```

**Правила привязки:**
- `assign_port(..., writer)` — ровно один раз для любого канала (ровно 1 writer).
- `assign_port(..., reader)` для `EventChannel` — ровно один раз (SPSC: ровно 1 reader).
- `assign_port(..., reader)` для `StateChannel<T, N>` — ровно N раз (по одному на каждого reader).
- Порядок привязок не важен, все привязки должны быть выполнены до `seal()`.
- Повторный `assign_port` для уже привязанного слота (rebind) — запрещён в v1.
- Направление проверяется типовой системой:
  - `writer`-сторона принимает только `OutPort<T>`
  - `reader`-сторона принимает только `InPort<T>`

---

## 5. Шаг 4 — Зарегистрировать каналы в реестре

После привязки каналы регистрируются в реестре:

```cpp
registry.add(ch_temp);
registry.add(ch_pressure);
registry.add(ch_cmd);
```

**Правила регистрации:**
- Каналы регистрируются **после** задач (`registry.add(task_desc, wrapper)`).
- Порядок `registry.add(channel)` фиксирован в `registry_report` (секция `channels`).
- Один `registry.add(channel)` — один канал. Повторная регистрация того же канала — ошибка.
- Регистрация канала до привязки (`assign_port`) не запрещена, но `seal()` проверит полноту привязки.

---

## 6. Шаг 5 — seal()

`seal()` проверяет полноту и корректность всей конфигурации каналов.

**Для каналов `seal()` выполняет:**

| Проверка | EventChannel | StateChannel<T,N> |
|----------|-------------|-------------------|
| Writer привязан | `writers_bound == 1` | `writers_bound == 1` |
| Reader(s) привязан(ы) | `readers_bound == 1` | `readers_bound == N` |
| Нет незаполненных слотов | `true` | `true` |
| Тип T совпадает у writer и reader | `true` | `true` |

Любое несоответствие → `SealError` с указанием канала и причины:
```
"ch_temp: readers_bound=0, expected=1"
"ch_cmd: writer not bound"
```

Все ошибки возвращаются списком, не только первая.

**Что `seal()` НЕ проверяет:**
- Наличие опубликованных данных в `StateChannel` — это runtime-семантика.
- Наличие элементов в `EventChannel` — очередь может быть пустой при старте.

После успешного `seal()` конфигурация каналов заморожена навсегда.

---

## 7. Шаг 6 — Использование в RT (step)

После `seal()` задачи работают через pre-bound handles напрямую.
В `step()` запрещены вызовы конфигурационного API реестра (`assign_port`, `add`, `seal`).
Вызов `registry.signal(task_index)` является scheduling-примитивом — не канальным API; допустим в RT.

### EventChannel — API в RT

```cpp
// Writer (OutPort<T>):
bool ok = port.push(value);   // false = буфер полон (LastLoss — элемент отброшен); штатно

// Reader (InPort<T>):
T item{};
bool got = port.pop(item);    // false = очередь пуста; штатно; retry на следующем тике
```

### StateChannel — API в RT

```cpp
// Writer (OutPort<T>):
port.publish(value);          // всегда успешно; нет возврата ошибки

// Reader (InPort<T>):
T snapshot{};
bool got = port.try_read(snapshot);  // false = no-data; штатно; retry на следующем тике
```

**Контракт `false`:**
- `false` — не ошибка. Это штатный результат при отсутствии данных.
- Причины `false` у `StateChannel.try_read()`:
  1. До первого `publish()` — данных ещё нет.
  2. На SMP без Condition B — гонка на узком окне (допустима по контракту примитива).
- Задача **обязана** обработать `false` без паники. Стратегия: retry при следующем тике.

---

## 8. Полный пример init()

```cpp
// --- Статические объекты ---
SensorPayload  sensor_payload;
CtrlPayload    ctrl_payload;
LoggerPayload  logger_payload;

StateChannel<float, 1>             ch_temp;
StateChannel<float, 1>             ch_pressure;
EventChannel<Command, 8, LastLoss> ch_cmd;

TaskWrapper<SensorPayload>  sensor_wrapper{sensor_payload};
TaskWrapper<CtrlPayload>    ctrl_wrapper{ctrl_payload};
TaskWrapper<LoggerPayload>  logger_wrapper{logger_payload};

// --- init() ---
void init() {
    // 1. Дескрипторы задач
    TaskDescriptor sensor_desc{
        .task_name    = "SNSR",
        .core_id      = 0,
        .priority     = 5,
        .period_ticks = 100,
        .observable   = true,
        .timeout_ticks = 150,
    };
    TaskDescriptor ctrl_desc{
        .task_name    = "CTRL",
        .core_id      = 1,
        .priority     = 10,
        .period_ticks = 0,   // SignalDriven
        .observable   = false,
    };
    TaskDescriptor logger_desc{
        .task_name    = "LOGR",
        .core_id      = 1,
        .priority     = 1,
        .period_ticks = 1000,
        .observable   = false,
    };

    // 2. Регистрация задач (attach_hb вызывается внутри registry.add)
    registry.add(sensor_desc,  sensor_wrapper);
    registry.add(ctrl_desc,    ctrl_wrapper);
    registry.add(logger_desc,  logger_wrapper);

    // 3. Привязка каналов к портам
    registry.assign_port("SNSR", "temp_out",     ch_temp,     writer);
    registry.assign_port("CTRL", "temp_in",      ch_temp,     reader);

    registry.assign_port("SNSR", "pressure_out", ch_pressure, writer);
    registry.assign_port("CTRL", "pressure_in",  ch_pressure, reader);

    registry.assign_port("CTRL", "cmd_out",      ch_cmd,      writer);
    registry.assign_port("LOGR", "cmd_in",       ch_cmd,      reader);

    // 4. Регистрация каналов
    registry.add(ch_temp);
    registry.add(ch_pressure);
    registry.add(ch_cmd);

    // 5. Инициализация payload-ов (если есть init())
    sensor_wrapper.init();
    ctrl_wrapper.init();
    logger_wrapper.init();

    // 6. Заморозка
    auto result = registry.seal();
    if (!result.ok()) {
        handle_fatal(result.errors());
    }
}
```

---

## 9. Взаимосвязи с другими структурами

### 9.1 Каналы и TaskWrapper

TaskWrapper не знает о каналах. Каналы принадлежат payload.
Wrapper вызывает `payload.step(now)` — payload сам обращается к портам.

```
scheduler → wrapper.step(now) → payload.step(now) → port.push()/try_read()
```

### 9.2 Каналы и реестр

Реестр хранит `ChannelDescriptor` для каждого канала (имя, тип, writer, readers).
Реестр не участвует в RT-обмене данными — только в bootstrap и мониторинге.

Аннотация `intra-core` / `inter-core` вычисляется при `seal()` из `core_id` writer и readers.
Доступна safety-монитору и диагностике. В v1 не переключает реализацию.

### 9.3 Каналы и heartbeat

Heartbeat не связан с каналами. Задача считается живой, если `step()` был вызван
в пределах `timeout_ticks`. Это инвариант wrapper — не канала.

### 9.4 Каналы и сигнальная маска

SignalDriven-задача может быть активирована по событию от другой задачи.
Механизм: sender вызывает `registry.signal(task_index)` после `push()` — это scheduling-вызов, не конфигурационный API.
Данный вызов допустим в `step()` и не противоречит правилу раздела 7.
`task_index` и сигнальная маска не связаны с каналами напрямую; канал — только транспорт данных.

---

## 10. Порядок действий при добавлении нового канала

```
1. Определить тип канала (EventChannel / StateChannel) и семантику данных T
2. Для StateChannel: посчитать точное число reader-задач → задать N в типе
3. Для EventChannel: выбрать Capacity (степень двойки ≥ 2)
4. Добавить OutPort<T> в payload writer-задачи
5. Добавить InPort<T> в payload каждой reader-задачи
6. Создать объект канала с lifetime не короче runtime (обычно static storage)
7. Выполнить assign_port для writer и всех readers в init()
8. Вызвать registry.add(channel) в init()
9. Убедиться, что payload.step() использует порт корректно
10. Запустить init() + seal() — убедиться, что все проверки зелёные
```

---

## 11. Запрещённые операции

| Операция | Что произойдёт |
|----------|----------------|
| `assign_port` после `seal()` | Ошибка конфигурации; sealed-реестр read-only |
| Повторный `assign_port` для уже привязанного слота (rebind) | Запрещён в v1 |
| `assign_port(..., reader)` для `EventChannel` дважды | Нарушает SPSC-контракт → SealError |
| `assign_port(..., reader)` для `StateChannel<T,N>` N+1 раз | Нарушает контракт → SealError |
| `assign_port(..., reader)` для `StateChannel<T,N>` N-1 раз | Незаполненный слот → SealError |
| Копирование `InPort` / `OutPort` | Compile error (non-copyable) |
| Два `publish()` параллельно из двух задач | UB в примитиве (1-writer контракт) |
| Вызов `port.push()` из reader-задачи | Нарушение направления порта |

---

## 12. Что разработчик НЕ делает с каналами

- Не указывает `intra-core` / `inter-core` явно — это аннотация, выводится автоматически.
- Не обязан инициализировать `StateChannel` начальным значением — `false` до первого `publish()` является нормой.
- Не паникует при `false` из `try_read()`/`pop()` — это штатный результат.
- Не вызывает методы примитива (`SPSCRing`, `SPMCSnapshot`) напрямую — только через порты.
- Не передаёт указатели на payload-данные другим задачам напрямую — только через каналы.
- Не изменяет топологию каналов в runtime (после `seal()`).

---

---

*STAM Channel Technology v1*
*Составлено по итогам обсуждения: оператор, Клод (Claude Sonnet 4.6), Codex (GPT-5)*
