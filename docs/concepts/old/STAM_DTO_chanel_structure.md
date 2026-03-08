# LEGACY NOTE

This document is archived and kept for historical context.
It may describe pre-refactor APIs (e.g., assign_port/type-erased handle pipeline, old header paths).

Current source-of-truth documents are:
- stam-rt-lib contracts: stam-rt-lib/docs/*
- primitives contracts: primitives/docs/*
- architecture overview: docs/architecture/*

---

# Channel Structure — Контракты и инварианты (STAM v1)

*Версия: v1. Источник: fixed.md (FIXED-CHAN-001..007) + сессии 00001–00019.*

---

## 1. Назначение

Каналы — единственный механизм обмена данными между задачами в STAM.
Канал соединяет ровно одного производителя (writer) с одним или несколькими потребителями (readers).

Каналы не являются очередью сообщений общего назначения. Их семантика жёстко определяется типом.

---

## 2. Таксономия каналов

| Тип | Семантика | Примитив | Writer | Readers |
|-----|-----------|----------|--------|---------|
| `EventChannel<T, C, P>` | FIFO-очередь (каждый элемент доставляется ровно одному reader) | `SPSCRing<T, C>` | 1 | 1 |
| `StateChannel<T, N>` | Снимок состояния (latest-wins; все readers получают последнее значение) | `SPMCSnapshot<T, N>` | 1 | N (точное число) |

---

## 3. Типы каналов

### 3.1 EventChannel

```
EventChannel<T, Capacity, DropPolicy>
```

**Параметры:**
- `T` — тип полезной нагрузки. Обязательно `trivially_copyable`.
- `Capacity` — размер кольцевого буфера. Степень двойки, ≥ 2. Полезная ёмкость = Capacity − 1.
- `DropPolicy` — политика при переполнении буфера. v1 содержит только `LastLoss`.

**Семантика:**
- FIFO-очередь. Каждый `push()` помещает элемент в буфер; каждый `pop()` извлекает один элемент.
- Промежуточные элементы не теряются, пока буфер не заполнен.
- При переполнении поведение определяется `DropPolicy`:
  - `LastLoss` — новый элемент отбрасывается, `push()` возвращает `false`.
  - `FirstLoss` — *планируется в v2; примитив не реализован*.

**Контракт read/write (RT):**
- `push()` → `bool`. `false` = штатный результат при полном буфере (не ошибка).
- `pop()` → `bool`. `false` = штатный результат при пустом буфере (не ошибка).
- Оба вызова: wait-free, O(1), детерминированный WCET.

**Топология:**
- Ровно 1 writer, ровно 1 reader. `seal()` проверяет выполнение этого условия.

---

### 3.2 StateChannel

```
StateChannel<T, N>
```

**Параметры:**
- `T` — тип полезной нагрузки. Обязательно `trivially_copyable`.
- `N` — точное число reader-задач. Должно совпадать с числом привязанных readers на момент `seal()`.

**Семантика:**
- Снимок состояния (latest-wins). Промежуточные обновления могут быть потеряны.
- Все N readers независимо читают последнее опубликованное значение.
- `publish()` — атомарная публикация нового снимка без потери целостности читаемых данных.

**Контракт read/write (RT):**
- `publish(value)` — всегда успешна (wait-free, O(1)). Нет возврата ошибки.
- `try_read(out)` → `bool`:
  - `false` (до первой публикации): штатный результат; данных ещё нет.
  - `false` (на SMP без Condition B): штатный результат; см. п. 6.
  - `true` — `out` содержит консистентный снимок последнего опубликованного значения.

**Ограничение N:**
- `N` задаётся на этапе компиляции.
- `N` — точное число reader-задач, не верхняя граница. Нарушение → UB в примитиве (Slot Availability Theorem).
- `seal()` принудительно проверяет равенство; несоответствие → `SealError`.

**Начальное состояние:**
- `initial_value` в v1 не требуется. До первого `publish()` канал находится в состоянии `unpublished`.
- Получение `false` при `try_read()` до первого `publish()` — норма, не ошибка.

**Топология:**
- Ровно 1 writer, ровно N readers. `seal()` проверяет выполнение этого условия.

---

## 4. Порты, PortName и BindResult

### 4.1 Типы портов

Четыре типа портов, строго типизированных по виду канала и направлению:

| Тип | Канал | Направление | RT-метод |
|-----|-------|-------------|----------|
| `EventOutPort<T>` | `EventChannel` | writer | `push(T) → bool` |
| `EventInPort<T>`  | `EventChannel` | reader | `pop(T&) → bool` |
| `StateOutPort<T>` | `StateChannel` | writer | `publish(T) → void` |
| `StateInPort<T>`  | `StateChannel` | reader | `try_read(T&) → bool` |

**Природа:**
- Порт — лёгкий handle (wrapper) на Writer или Reader примитива.
- Порт не владеет хранилищем канала. Буфер принадлежит каналу.
- Порты non-copyable (роль writer/reader уникальна и единственна).
- Тип порта является документацией: компилятор запрещает `push()` на `StateOutPort<T>`.

### 4.2 PortName

`PortName` — 4-символьный идентификатор порта, хранится как `uint32_t` (fourcc). Алфавит: `[A-Z0-9_]`.

```cpp
struct PortName {
    uint32_t value;
    constexpr explicit PortName(const char (&s)[5]) noexcept
        : value(uint32_t(s[0])<<24 | uint32_t(s[1])<<16 | uint32_t(s[2])<<8 | s[3])
    {}
    constexpr bool operator==(PortName o) const noexcept { return value == o.value; }
};
```

Сравнение `PortName` — одна инструкция (`uint32_t` compare). Нет строковых операций в bootstrap.

### 4.3 BindResult

```cpp
enum class BindResult : uint8_t {
    ok,
    payload_has_no_ports,  // bind_port_fn == nullptr в TaskWrapperRef
    unknown_port,          // PortName не распознан в payload
    type_mismatch,         // TypeErasedHandle несёт не тот тип (debug-assert в release)
    already_bound,         // порт уже привязан (rebind запрещён в v1)
};
```

### 4.4 Объявление в payload

Payload объявляет порты как public-поля и реализует `bind_port()`:

```cpp
struct SensorPayload {
    // compile-time PortName-константы
    static constexpr PortName SOUT{"SOUT"};
    static constexpr PortName CINP{"CINP"};

    EventOutPort<SensorData>  sensor_out;
    StateInPort<CtrlData>     ctrl_in;

    // Опциональный метод — если отсутствует, bind_port_fn в TaskWrapperRef = nullptr
    BindResult bind_port(PortName name, TypeErasedHandle h) noexcept {
        if (name == SOUT) return sensor_out.bind(h);
        if (name == CINP) return ctrl_in.bind(h);
        return BindResult::unknown_port;
    }
};
```

`bind_port()` — опциональный метод payload. Задачи без каналов его не реализуют.

### 4.5 TypeErasedHandle

`TypeErasedHandle` — type-erased handle на Writer или Reader примитива. Несёт `void*` и `type_id`
для debug-валидации. Детали реализации `type_id_of<H>()` (без RTTI) — отдельный документ.

```cpp
struct TypeErasedHandle {
    void*    ptr;
    uint32_t type_id;  // compile-time id типа, для debug-assert

    template<typename H>
    BindResult bind_into(H& port) noexcept;  // проверяет type_id, записывает handle в порт
};
```

---

## 5. Жизненный цикл канала

```
[DECLARE]  →  [ASSIGN]  →  [SEAL]  →  [RUN]
```

### 5.1 DECLARE — объявление слотов в payload

Payload задачи объявляет ожидаемые порты как public-поля одного из типов:
`EventOutPort<T>`, `EventInPort<T>`, `StateOutPort<T>`, `StateInPort<T>`.
Это задаёт имя, тип данных `T`, вид канала и направление порта.

### 5.2 ASSIGN — привязка в bootstrap-фазе

Bootstrap вызывает `registry.assign_port(task_desc, PortName, channel, side) → BindResult`.

Цепочка вызовов:
```
registry.assign_port(task_desc, PortName{"SOUT"}, sensor_ch, writer_side)
  → wrapper_ref.bind_port_fn == nullptr?  → BindResult::payload_has_no_ports
  → wrapper_ref.bind_port_fn(obj, PortName{"SOUT"}, TypeErasedHandle{...})
      → payload.bind_port(PortName{"SOUT"}, handle)
          → sensor_out.bind(handle)  → channel помечает writer как bound
          → BindResult::ok
```

`assign_port()` возвращает `BindResult` немедленно. Ошибка видна bootstrap-коду до `seal()`.

**Инварианты ASSIGN:**
- Происходит **до** `seal()`.
- Один `assign_port()` — один порт — один слот.
- Повторный `assign_port()` (rebind) до `seal()` — запрещён в v1 (`BindResult::already_bound`).
- После `seal()` — любые `assign_port()` запрещены.

### 5.3 SEAL — валидация конфигурации

`seal()` — единственный момент структурной валидации всей системы каналов.

Канал реализует метод `is_fully_bound() noexcept → bool`:
- `EventChannel`: `writer_bound && reader_bound`
- `StateChannel<T,N>`: `writer_bound && readers_bound_count == N`

**Что проверяет `seal()` для каналов:**
- Для каждого канала: `channel.is_fully_bound() == true`; иначе → `SealError` с именем канала и причиной.
- Любой незаполненный порт (слот без handle) → `SealError` с указанием канала и причины.

`seal()` проверяет **структурную** корректность графа. Наличие фактически опубликованных данных не проверяется (это runtime-семантика).

### 5.4 RUN — runtime

После `seal()` задачи работают исключительно через pre-bound handles.
RT-код использует `port.push()` / `port.pop()` / `port.publish()` / `port.try_read()` напрямую.
Реестр и конфигурация каналов в RT-фазе не трогаются.

---

## 6. SMP-ограничения

### 6.1 EventChannel на SMP

`SPSCRing` — wait-free на уни- и мультипроцессоре.
Корректность обеспечивается memory ordering (acquire/release на `head_` и `tail_`).
Топология 1W/1R выполняется конструктивно.

### 6.2 StateChannel на SMP

`SPMCSnapshot` — wait-free при соблюдении условий корректности:

**Condition A (однопроцессор):** отключение preemption в критической секции (steps 2–3) достаточно для исключения гонки. Полная корректность гарантирована.

**Condition B (SMP с временным разделением):** архитектура гарантирует, что writer завершает `publish()` до того, как readers начинают новый polling-тик. Корректность гарантирована.

**SMP без Condition A или B:**
- Preemption disable недостаточен при физическом параллелизме ядер.
- `try_read()` может вернуть `false` даже после первого `publish()`.
- Это **штатное поведение** (not wait-free by Herlihy; false = no-data, не ошибка).
- Обработка `false` в reader — ответственность разработчика задачи (v1: retry при следующем тике).

**Итог для v1:** `try_read()` возвращающий `false` — штатный результат для `StateChannel` при любых SMP-условиях. Задачи обязаны обрабатывать `false` без паники.

---

## 7. Отображение на примитивы

| Канал | Примитив | Файл |
|-------|----------|------|
| `EventChannel<T, C, LastLoss>` | `SPSCRing<T, C>` | `primitives/spsc_ring.hpp` |
| `StateChannel<T, N>` | `SPMCSnapshot<T, N>` | `primitives/spmc_snapshot.hpp` |

**Требования к T для обоих типов каналов:**
- `std::is_trivially_copyable<T>` — проверяется через `static_assert` в Core.

**Требования к Capacity (EventChannel):**
- Степень двойки, ≥ 2. Полезная ёмкость = Capacity − 1.

**Требования к N (StateChannel):**
- N ≥ 1 (хотя бы 1 reader).
- K = N + 2 ≤ 32 (ограничение busy_mask в `uint32_t`). Итого: N ≤ 30.
- N ≤ 254 (ограничение `refcnt` в `uint8_t`).

---

## 8. Аннотация intra/inter-core

Классификация `intra-core` / `inter-core` — производная аннотация реестра.
Вычисляется из `CoreId` writer-задачи и reader-задачи(й) при `seal()`.

В v1 не переключает реализацию канала. Оба типа используют одну SMP-ready реализацию.
Аннотация доступна для safety-монитора и диагностики.

---

## 9. Запрещённые операции

| Операция | Причина |
|----------|---------|
| `assign_port()` после `seal()` | Нарушает read-only инвариант sealed-реестра |
| Повторный `assign_port()` для уже привязанного слота (rebind) | Запрещён в v1 |
| Вызов `writer()` примитива более одного раза | Нарушает контракт единственного producer (1W) |
| Вызов `reader()` у `EventChannel` более одного раза | Нарушает SPSC-контракт (ровно 1 reader) |
| Вызов `reader()` у `StateChannel<T, N>` более `N` раз | Нарушает контракт `StateChannel<T, N>` |
| Создание копии порта (`EventOutPort`, `EventInPort`, `StateOutPort`, `StateInPort`) | non-copyable; нарушает уникальность роли writer/reader |
| `StateChannel<T, N>` с `N` > реального числа readers (при корректном `seal()`) | `SealError` (`readers_bound < N`) |
| `StateChannel<T, N>` с `N` < реального числа readers | SealError (`readers_bound > N`) |
| Параллельный `publish()` из двух producer-задач | Нарушает контракт 1-writer; UB в примитиве |

---

## 10. Связь с реестром

- Реестр хранит `ChannelDescriptor` для каждого канала.
- `ChannelDescriptor` содержит: имя канала, тип, тип данных, ссылки на Writer/Reader handles.
- `seal()` реестра проходит по всем `ChannelDescriptor` и выполняет проверки из п. 5.3.
- После `seal()` реестр — read-only таблица; topology annotation (`intra/inter`) доступна runtime.

---

## Приложение A: Invariants summary

| ID | Инвариант |
|----|-----------|
| C-I1 | Канал принадлежит ровно одному writer-handle |
| C-I2 | `EventChannel` принадлежит ровно одному reader-handle |
| C-I3 | `StateChannel<T,N>` принадлежит ровно N reader-handles |
| C-I4 | Порт — handle, не буфер; хранилище принадлежит каналу |
| C-I5 | Привязка портов — только в bootstrap (до `seal()`) |
| C-I6 | `seal()` = единственная точка структурной валидации |
| C-I7 | `false` из `pop()`/`try_read()` — штатный результат, не ошибка |
| C-I8 | `T` обязательно `trivially_copyable` для всех каналов v1 |
