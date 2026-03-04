# TaskWrapper — Структура и контракты

*STAM System Registry v1*

---

## 1. Назначение

`TaskWrapper<Payload>` — адаптер между системным реестром и пользовательским кодом задачи.

Wrapper:
- предоставляет реестру единый интерфейс вызова задачи (`step`, `init`, `alarm`, `done`)
- обновляет heartbeat-атомик после каждого шага (механизм наблюдаемости)
- изолирует реестр и scheduler от деталей конкретного payload

Wrapper **не** является механизмом планирования. Он не знает о периоде, приоритете или ядре — это данные реестра.

---

## 2. Расположение в кодовой базе

```
stam/exec/tasks/taskwrapper.hpp   — шаблон TaskWrapper<Payload>
stam/model/tags.hpp               — концепты: Steppable, RtSafe, RtHooks, RtPayload
```

---

## 3. Концепты и требования к Payload

### 3.1 Обязательный концепт — `Steppable`

```cpp
template<class T>
concept Steppable =
    requires(T& t, tick_t now) {
        { t.step(now) } noexcept -> std::same_as<void>;
    };
```

Каждый payload **обязан** реализовать:
```cpp
void step(tick_t now) noexcept;
```

`now` — текущее время в тиках (`tick_t`), передаётся планировщиком.

### 3.2 Концепт для RT-задач — `RtPayload`

```cpp
// rt_safe_tag — маркер RT-безопасности
struct rt_safe_tag {};

template<class T>
concept RtSafe =
    requires { typename T::rt_class; } &&
    std::same_as<typename T::rt_class, rt_safe_tag>;

// RtHooks: если опциональный хук объявлен, он должен быть noexcept
template<class T>
concept RtHooks =
    (!requires(T& t){ t.init();  } || requires(T& t){ { t.init()  } noexcept; }) &&
    (!requires(T& t){ t.alarm(); } || requires(T& t){ { t.alarm() } noexcept; }) &&
    (!requires(T& t){ t.done();  } || requires(T& t){ { t.done()  } noexcept; });

template<class T>
concept RtPayload = RtSafe<T> && Steppable<T> && RtHooks<T>;
```

Граница RT/Non-RT проходит **на уровне реестра**, а не wrapper-а:
- RT-реестр: `registry.add(desc, wrapper)` статически проверяет `RtPayload` у payload
- Non-RT реестр: достаточно `Steppable`

`TaskWrapper` остаётся единым — `TaskWrapper<Payload: Steppable>`. `RtPayload ⊇ Steppable`, поэтому RT-задача работает везде, где принимается `Steppable`.

### 3.3 Опциональные методы payload

Если payload реализует следующие методы, wrapper вызовет их автоматически:

| Метод | Когда вызывается | Назначение |
|-------|-----------------|------------|
| `void init() noexcept` | при инициализации задачи | настройка состояния перед первым step |
| `void alarm() noexcept` | по решению scheduler/монитора | реакция на превышение timeout |
| `void done() noexcept` | при штатном завершении | финализация |

Проверка наличия методов — `if constexpr`, без виртуальных вызовов.
Для RT-задач все реализованные хуки обязаны быть `noexcept` — это проверяется концептом `RtHooks`.

---

## 4. Интерфейс TaskWrapper

```cpp
namespace stam::exec::tasks {

template <stam::model::Steppable Payload>
class TaskWrapper {
public:
    // Конструктор — только payload, без heartbeat
    // hb привязывается внутренним кодом registry.add() через attach_hb()
    explicit TaskWrapper(Payload& payload) noexcept;

    // Запрет копирования и перемещения
    TaskWrapper(const TaskWrapper&)            = delete;
    TaskWrapper& operator=(const TaskWrapper&) = delete;
    TaskWrapper(TaskWrapper&&)                 = delete;
    TaskWrapper& operator=(TaskWrapper&&)      = delete;

    // --- Интерфейс вызова ---

    // Основной шаг задачи. Вызывается scheduler-ом.
    // Порядок: payload.step(now) → hb_->store(now, release)
    // Предусловие: attach_hb() уже вызван (assert в debug)
    void step(tick_t now) noexcept;

    // Инициализация (опционально в payload)
    void init()  noexcept;

    // Реакция на alarm (опционально в payload)
    void alarm() noexcept;

    // Завершение (опционально в payload)
    void done()  noexcept;

    // Привязка heartbeat — вызывается внутренним кодом registry.add()
    // Не предназначен для прямого вызова разработчиком
    void attach_hb(std::atomic<heartbeat_word_t>* hb) noexcept;

private:
    Payload&                          payload_;
    std::atomic<heartbeat_word_t>*    hb_ = nullptr;
};

} // namespace stam::exec::tasks
```

---

## 5. Heartbeat (hb_)

### 5.1 Тип heartbeat_word_t

```cpp
// Каноническое объявление — в model/tags.hpp
using heartbeat_word_t = stam::model::heartbeat_word_t;
using tick_t = stam::model::tick_t;

static_assert(std::atomic<heartbeat_word_t>::is_always_lock_free,
    "heartbeat_word_t must be lock-free atomic on this platform");
```

`heartbeat_word_t` — платформенный тип, равный ширине шины данных. Требует C++17.
На 32-битных MCU (Cortex-M4 и подобных) `is_always_lock_free` для `uint32_t` всегда `true` — это статическая гарантия, не runtime-проверка.

### 5.2 Владение

Атомики heartbeat **хранятся в реестре**. Задача и payload не владеют heartbeat и не передают его вручную.

Двухфазная инициализация:
```
фаза 1: TaskWrapper wrapper(payload)          // без hb, hb_ = nullptr
фаза 2: registry.add(task_desc, wrapper)      // внутренняя логика add выделяет hb и вызывает
                                               // wrapper.attach_hb(&hb_) внутри
```

После `registry.add()` wrapper хранит указатель на heartbeat-атомик реестра.
Внутренняя логика `registry.add()` вызывает `attach_hb()` для каждой задачи, независимо от `observable`.
После `seal()` все ссылки стабильны и неизменны.

### 5.3 Семантика записи и чтения

После каждого `step(now)` wrapper выполняет:
```cpp
hb_->store(now, std::memory_order_release);
```

Safety-монитор читает heartbeat:
```cpp
hb_ref.load(std::memory_order_acquire)
```

Цепочка доступа для монитора:
```
registry.task(task_index) → hb_ref.load(memory_order_acquire)
```

### 5.4 Проверка таймаута

```cpp
// Unsigned-разность корректно работает при переполнении счётчика
(now - hb_ref.load(memory_order_acquire)) > timeout_ticks
```

Оба операнда `heartbeat_word_t` (`uint32_t`). Вычитание выполняется по правилам беззнаковой арифметики — корректно при переполнении счётчика без специальной обработки.
Если превышено — safety-монитор вызывает `wrapper.alarm()`.

### 5.5 Lifetime

```
lifetime(hb_storage в реестре) >= lifetime(sealed_registry)
```

Heartbeat-атомики действительны на всё время работы sealed-реестра.

---

## 6. Жизненный цикл wrapper

```
[создание: TaskWrapper(payload)]            ← hb_ = nullptr
         ↓
[регистрация: registry.add(desc, wrapper)]  ← внутренняя логика add вызывает wrapper.attach_hb(&hb)
         ↓
[инициализация: wrapper.init()]             ← вызывается до первого step
         ↓
[seal()]                                    ← завершение bootstrap-фазы, переход в RT
         ↓
[исполнение: wrapper.step(now)]             ← вызывается scheduler-ом
         ↓     ↑
[alarm при нарушении timeout: wrapper.alarm()]
         ↓
[завершение: wrapper.done()]               ← при штатной остановке
```

---

## 7. Инварианты

- Wrapper не копируется и не перемещается.
- `step()` **запрещён до `attach_hb()`** — в debug-сборке `assert(hb_ != nullptr)` в начале `step()`. В release assert вырезается — zero-cost.
- `step()` всегда обновляет `hb_` после вызова `payload_.step(now)`.
- `payload_` должен оставаться валидным на всё время жизни wrapper.
- `hb_` (указатель на атомик реестра) действителен после `registry.add()` и на всё время жизни sealed-реестра.
- Wrapper не вызывает `step()` самостоятельно — это обязанность scheduler-а.
- `attach_hb()` вызывается внутренним кодом `registry.add()` ровно один раз. Повторный вызов — ошибка.
- `init()` относится к bootstrap-фазе и выполняется до `seal()`.

---

*STAM TaskWrapper v1 — Structure & Contracts*
*Составлено по итогам обсуждения: оператор, Клод (Claude Sonnet 4.6), Codex (GPT-5)*
