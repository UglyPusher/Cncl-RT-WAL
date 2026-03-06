# System Registry — Структура и контракты

*STAM System Registry v1*

---

## 1. Назначение

Системный реестр — статическое описание конфигурации RT/Non-RT системы исполнения.

Реестр хранит:
- дескрипторы задач (с параметрами исполнения и ссылками на wrapper)
- дескрипторы каналов связи (с топологией)
- сигнальную маску (после `seal()`)

Реестр **не исполняет** задачи. После `seal()` — только read-only таблица для runtime.

---

## 2. Состояния реестра

```
[OPEN]  →  seal()  →  [SEALED]
  ↑                       ↑
add/remove/assign_port только чтение
разрешены               runtime использует
```

| Состояние | Операции | Кто использует |
|-----------|----------|----------------|
| `OPEN` | add, remove, assign_port | разработчик в `init()` |
| `SEALED` | read-only view | scheduler, safety-монитор, runtime |

Повторный вызов `seal()` — ошибка. `init()` вызывается ровно один раз.
Порядок задач для tiebreaker задаётся только порядком `add()` и внутренней сортировкой в `seal()`.

---

## 3. TaskDescriptor — дескриптор задачи

Каждая зарегистрированная задача описывается дескриптором:

```
TaskDescriptor {
    // --- Задаёт разработчик ---
    task_name    : const char*        // уникальный идентификатор, обязателен (не nullptr)
    core_id      : CoreId             // ядро исполнения, явно задаётся разработчиком
    priority     : uint8_t            // статический приоритет
    period_ticks : uint32_t           // 0 = SignalDriven; ≥ 1 = Periodic
    observable   : bool               // нужна ли watchdog-наблюдаемость
    timeout_ticks: uint32_t           // только если observable == true; иначе игнорируется
    ports        : PortList           // типизированные порты (EventOutPort / EventInPort / StateOutPort / StateInPort)
    wrapper_ref  : TaskWrapperRef     // ссылка на TaskWrapper задачи

    // --- Реестр присваивает ---
    task_index   : uint8_t            // при seal(); = позиция бита в signal_mask_t
    hb_ref       : atomic<heartbeat_word_t>&  // при registry.add()
}
```

`task_name` задаётся разработчиком, обязателен и должен быть уникален в реестре (проверяется по содержимому строки при `add()`).
`task_index` **не задаётся разработчиком** — присваивается при `seal()`; является одновременно позицией бита в сигнальной маске (`signal_bit == task_index`, v1 constraint).

---

## 4. ChannelDescriptor — дескриптор канала

```
ChannelDescriptor {
    name         : const char*   // человекочитаемая метка (flash), опционально (nullptr допустим)
    channel_type : ChannelType   // EventChannel | StateChannel
    data_type    : TypeId        // тип T (payload)
    writer       : PortRef       // ссылка на EventOutPort / StateOutPort задачи-writer
    readers      : PortRef[]     // ссылки на EventInPort / StateInPort задач-readers
}
```

Ownership по типу канала:

| Тип | writer | readers |
|-----|--------|---------|
| `EventChannel<T>` | ровно 1 | ровно 1 |
| `StateChannel<T, N>` | ровно 1 | ровно N (точное число, задаётся при объявлении канала) |

Топологический класс (`intra-core` / `inter-core`) **выводится** из `core_id` writer и readers — не задаётся.

---

## 5. Порты

Четыре типа портов, строго типизированных по виду канала:

```
EventOutPort<T>  — writer-сторона EventChannel  (метод: push(T) → bool)
EventInPort<T>   — reader-сторона EventChannel  (метод: pop(T&) → bool)
StateOutPort<T>  — writer-сторона StateChannel  (метод: publish(T) → void)
StateInPort<T>   — reader-сторона StateChannel  (метод: try_read(T&) → bool)
```

Порты объявляются как public-поля payload-объекта задачи.
Тип порта является **типовой гарантией**: компилятор запрещает вызов `push()` на `StateOutPort`.

Идентификация порта — через `PortName` (4-символьный ASCII, `uint32_t` fourcc):

```cpp
// Payload объявляет compile-time константы:
static constexpr PortName SOUT{"SOUT"};
static constexpr PortName CINP{"CINP"};
```

Привязка выполняется через `registry.assign_port(task_desc, PortName, channel, side)`.
Направление (`writer`/`reader`) валидируется при `assign_port()` типовой системой.
Полнота привязки — при `seal()` через `channel.is_fully_bound()`.

---

## 6. Интерфейс реестра

### 6.1 Pre-seal (OPEN)

```cpp
// Регистрация задачи с wrapper (связка task↔wrapper формируется при add)
registry.add(TaskDescriptor& task, TaskWrapperRef wrapper);

// Регистрация канала (по одной)
registry.add(ChannelDescriptor& channel);

// Привязка канала к порту задачи — возвращает BindResult немедленно
BindResult registry.assign_port(TaskDescriptor& task, PortName port,
                                ChannelDescriptor& channel, PortSide side) noexcept;
```

Пример:
```cpp
// Bootstrap:
registry.assign_port(task_desc_a, SensorPayload::SOUT, sensor_ch, writer_side);
registry.assign_port(task_desc_b, SensorPayload::CINP, sensor_ch, reader_side);
```

Цепочка внутри `assign_port()`:
```
1. wrapper_ref.bind_port_fn == nullptr → BindResult::payload_has_no_ports
2. wrapper_ref.bind_port_fn(obj, port, handle) → payload.bind_port(port, handle)
3. channel помечает соответствующую сторону как bound
4. возвращает BindResult из payload.bind_port()
```

Контракт по payload-проверкам задаётся типом реестра:

```cpp
// RT-контур: только RtPayload
rt_registry.add(task_desc_rt, task_wrapper_rt);      // Payload удовлетворяет RtPayload

// Non-RT контур: достаточно Steppable
nonrt_registry.add(task_desc_nonrt, task_wrapper_nonrt); // Payload удовлетворяет Steppable
```

`TaskWrapper` остаётся общим; граница RT/Non-RT проходит на уровне `registry.add(...)`.
В этом же шаге внутренняя логика `registry.add(...)` выделяет heartbeat для задачи
и вызывает `wrapper.attach_hb(...)`.
Проверка уникальности `task_name` (по содержимому строки) выполняется немедленно при `add(...)`.

### 6.2 Заморозка

```cpp
// Финализация реестра — вызывается один раз
SealResult registry.seal();
```

`SealResult` содержит список ошибок (пустой = успех).
Каждая ошибка указывает сущность и причину: `"ch_temp: нет writer"`.

### 6.3 Post-seal (SEALED) — read-only

```cpp
// Доступ к задаче по индексу
const TaskDescriptor& registry.task(uint8_t idx) const;

// Поиск задачи по имени → task_index (выполняется один раз в bootstrap, не в RT)
uint8_t registry.find(const char* task_name) const;

// Итерация по задачам
TaskView registry.tasks() const;

// Итерация по каналам
ChannelView registry.channels() const;

// Сигнальная маска (все биты задач)
signal_mask_t registry.signal_mask() const;

// Маска задач на конкретном ядре
signal_mask_t registry.core_mask(CoreId core) const;

// Scheduling-примитив: установить сигнальный бит задачи (RT-безопасно)
// Допускается вызов из step() и ISR; не является конфигурационным API
void registry.signal(uint8_t task_index) noexcept;
```

---

## 7. seal() — внутренний порядок шагов

Предусловие: все зарегистрированные wrapper-ы уже инициализированы (`wrapper.init()` вызван до `seal()`).

```
1. Сортировка задач по приоритету
   компаратор: primary   = priority (убывание)
               secondary = порядок registry.add() (возрастание, tiebreaker)
        ↓
2. Присвоение task_index : uint8_t каждой задаче
   signal_bit == task_index  (v1 constraint, derived — не хранится как отдельное поле)
        ↓
3. Публикация системных ссылок в sealed-view
   wrapper_ref и hb_ref (для всех задач), собранные на этапе add(),
   становятся read-only и неизменными после seal()
        ↓
4. Построение сигнальной маски
        ↓
5. Проверка структурных инвариантов (см. раздел 8)
   → при любом нарушении: добавить в список ошибок
        ↓
6. Перевод реестра в SEALED (read-only)
```

После перехода в `SEALED` фаза bootstrap завершена; `init()`-хуки wrapper-ов в RT-фазе не вызываются.

---

## 8. Инварианты, проверяемые при seal()

**Ownership и типы каналов:**

| Инвариант | Условие |
|-----------|---------|
| Ownership EventChannel | WriterCount == 1, ReaderCount == 1 |
| Ownership StateChannel | WriterCount == 1, ReaderCount == N |
| Типовая корректность | тип T совпадает у writer и всех readers |
| Writer задачи существует | writer task зарегистрирована в реестре |
| Reader задачи существуют | каждый reader task зарегистрирован в реестре |

**Параметры задач:**

| Инвариант | Условие |
|-----------|---------|
| CoreId корректен | core_id < NUM_CORES |
| Periodic: период | period_ticks ≥ 1 |
| Timeout задан | observable == true → timeout_ticks задан |
| Ёмкость маски | task_count ≤ SIGNAL_MASK_WIDTH |

**Портовый слой:**

| Инвариант | Условие |
|-----------|---------|
| PortName валиден | 4 символа `[A-Z0-9_]` |
| Тип порта соответствует каналу | `EventOutPort` ↔ `EventChannel`, `StateInPort` ↔ `StateChannel` и т.д. |
| Канал полностью связан | `channel.is_fully_bound() == true` при `seal()` |
| Нет rebind | повторный `assign_port()` для уже привязанного порта → `BindResult::already_bound` |
| Уникальность task_name | повторный add() задачи с тем же task_name — ошибка |
| Уникальность канала | повторный add() того же канала — ошибка |
| Стабильность ссылок post-seal | `wrapper_ref`/`hb_ref` назначены и не меняются после `seal()` |

---

## 9. Сигнальная маска

```
tick_t / heartbeat_word_t определяются в model/tags.hpp
signal_mask_t      = uint32_t
SIGNAL_MASK_WIDTH  = 32
signal_bit(task)   == task_index  (v1: производный, не хранится как отдельное поле)
static_assert(MAX_TASKS <= SIGNAL_MASK_WIDTH)
```

Маска строится при `seal()`. До `seal()` не существует.

`core_mask(core_id)` — подмаска задач, закреплённых за конкретным ядром.
Используется scheduler-ом для выбора задач к исполнению.

---

## 10. Heartbeat и наблюдаемость

Каждая задача имеет связанный heartbeat:

```
TaskDescriptor.hb_ref      → atomic<heartbeat_word_t>& (системное владение)
```

Цепочка доступа для safety-монитора:
```
registry.task(idx) → hb_ref.load(memory_order_acquire)
```

Safety-монитор анализирует heartbeat только для задач с `observable == true`.
Для задач с `observable == false` heartbeat поддерживается wrapper-ом, но не участвует в timeout-контроле.

Контракт владения:
- heartbeat хранится в системной области (реестр или heartbeat-подсистема runtime);
- задача/payload не владеет heartbeat;
- `TaskWrapper` пишет heartbeat через ссылку, опубликованную системой.
- тип `heartbeat_word_t` задаётся платформой (одним typedef/alias для проекта).

Контракт обновления и проверки:
- запись в wrapper: `hb_ref.store(now, memory_order_release)`;
- чтение в monitor: `hb_ref.load(memory_order_acquire)`;
- проверка таймаута: `(now - last_hb) > timeout_ticks` в `heartbeat_word_t` арифметике.

Формула с unsigned-разностью корректно работает при переполнении счётчика ticks.
Если превышено — вызывает `wrapper.alarm()`.

---

## 11. Registry Report (post-seal)

После `seal()` формируется детерминированный `registry_report`:

1. `tasks` (в порядке `task_index`):
   `task_index`, `task_name`, `registration_order`, `priority`, `core_id`, `period_ticks`, `observable`, `scheduler_domain`.
2. `channels` (в порядке `registry.add()`):
   `name`, `channel_type<T>`, `writer(port)`, `readers(ports)`.
3. `signal_mask`:
   итоговая маска в hex и по-битовой раскладке.

Формат отчёта стабилен между ревизиями и пригоден для CI-diff.

---

## 12. Зафиксированные решения v1

**TaskWrapperRef**
```
TaskWrapperRef {
    void* obj;
    void (*step_fn)     (void*, tick_t)                     noexcept;
    void (*init_fn)     (void*)                             noexcept;
    void (*alarm_fn)    (void*)                             noexcept;
    void (*done_fn)     (void*)                             noexcept;
    BindResult (*bind_port_fn)(void*, PortName, TypeErasedHandle) noexcept;  // nullptr = нет портов
}
```
Без virtual/RTTI/heap. Вызов идёт через явную таблицу функций.
`bind_port_fn = nullptr` если payload не реализует `bind_port()`.

**PortName**
```
PortName = 4 ASCII-символа [A-Z0-9_], хранится как uint32_t (fourcc)
Сравнение — одна uint32_t-инструкция. Нет строковых операций.
```

**BindResult**
```cpp
enum class BindResult : uint8_t {
    ok,
    payload_has_no_ports,  // bind_port_fn == nullptr
    unknown_port,          // PortName не распознан payload-ом
    type_mismatch,         // неверный тип handle (debug-assert)
    already_bound,         // rebind запрещён
};
```

**assign_port API**
```cpp
BindResult registry.assign_port(TaskDescriptor&, PortName, ChannelDescriptor&, PortSide) noexcept;
```

**Signal mask**
```
signal_mask_t     = uint32_t
SIGNAL_MASK_WIDTH = 32
static_assert(MAX_TASKS <= SIGNAL_MASK_WIDTH)
```

---

*STAM System Registry v1 — Structure & Contracts*
