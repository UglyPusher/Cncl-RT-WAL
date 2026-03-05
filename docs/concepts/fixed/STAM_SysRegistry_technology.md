# Технология работы разработчика с STAM System Registry v1

---

## 1. Принцип работы

Системный реестр — единственный источник истины о конфигурации системы.
Разработчик описывает систему **один раз** в функции `init()`.
После вызова `seal()` реестр заморожен и доступен runtime только на чтение.

```
init()  →  seal()  →  runtime
  ↑           ↑
 всё          реестр
 изменения    зафиксирован
 здесь        навсегда
```

`init()` вызывается ровно один раз на старте системы.

---

## 2. Структура функции init()

Функция `init()` всегда строится в одном порядке:

```
[ 1. Создание TaskDescriptor ]
[ 2. Создание TaskWrapper    ]
[ 3. Создание каналов        ]
[ 4. Регистрация задач+wrapper ]
[ 5. Связывание через порты  ]
[ 6. Регистрация каналов     ]
[ 7. Вызов wrapper.init()    ]
[ 8. seal()                  ]
```

Этот порядок фиксирован. Разработчик, читая `init()`, должен видеть соответствие
между кодом и топологической схемой системы (граф задач и каналов).

---

## 3. Объявление задачи

Задача создаётся с полным набором параметров:

```
Task(
    task_name,            // уникальный идентификатор, обязателен; проверяется при add
    core_id,              // ядро исполнения — явно, CoreId < NUM_CORES
    priority,             // статический приоритет uint8
    period_ticks,         // 0 = SignalDriven; ≥ 1 = Periodic
    observable,           // bool — нужна ли наблюдаемость safety-монитором
    timeout_ticks,        // только если observable == true
    ports: [PORT_A, PORT_B, ...]  // именованные типизированные порты задачи
)
```

**Тип задачи** выводится автоматически из `period_ticks`:
- `period_ticks == 0` → **SignalDriven**
- `period_ticks ≥ 1` → **Periodic**

**Порты** объявляются только здесь, при создании задачи.
Направление порта является типовой гарантией (например, `InPort<...>` / `OutPort<...>`), а не только соглашением об именовании.
`task_index` разработчик не задаёт — присваивается при `seal()`; позиция бита в сигнальной маске равна `task_index` (v1 constraint, не отдельное поле).
Heartbeat не передаётся в wrapper вручную разработчиком.
Связка `wrapper ↔ heartbeat` формируется реестром при регистрации задачи.

---

## 4. Объявление канала

```cpp
// Канал состояния — один writer, один или несколько readers
ch = StateChannel<T>()

// Канал события — один writer, один reader
ch = EventChannel<T>()
```

Тип данных `T` задаётся явно. Топологический класс (intra/inter-core)
выводится автоматически из `core_id` writer и readers — явного указания не требует.

---

## 5. Связывание задач и каналов через порты

```cpp
registry.assign_port("TASK_A", "OUT_PORT", ch, writer)   // writer — ровно один
registry.assign_port("TASK_B", "IN_PORT",  ch, reader)   // reader — повторяется для каждого
registry.assign_port("TASK_C", "IN_PORT",  ch, reader)   // reader 2
```

`assign_port(..., writer)` — привязка writer-стороны канала к порту задачи.
`assign_port(..., reader)` — привязка reader-стороны, вызывается для каждого reader отдельно.
Проверка направления выполняется типовой системой порта:
- `assign_port(..., writer)` принимает только `OutPort`
- `assign_port(..., reader)` принимает только `InPort`

Ownership:
- `EventChannel<T>`: ровно 1 `assign_port(..., writer)`, ровно 1 `assign_port(..., reader)`
- `StateChannel<T>`: ровно 1 `assign_port(..., writer)`, 1 или более `assign_port(..., reader)`

---

## 6. Регистрация

Задачи и каналы регистрируются по одному:

```cpp
registry.add(task_a_desc, task_a_wrapper)
registry.add(task_b_desc, task_b_wrapper)
registry.add(task_c_desc, task_c_wrapper)

registry.add(ch_1)
registry.add(ch_2)
```

До `seal()` реестр открыт: можно добавлять, удалять и перепривязывать задачи и каналы.
Ограничений нет — `signal_bit` в этой фазе не существует.

**Одно правило для tiebreaker:**
задачи с одинаковым приоритетом получат `task_index` в порядке `registry.add()`.
Относительный порядок `add()` для задач с равным приоритетом не менять.
Дублирование `task_name` блокируется сразу на `registry.add(...)` (по содержимому строки).

**Порядок относительно `assign_port`:**
`registry.add(task, wrapper)` должен быть вызван **до** `registry.assign_port(...)` для этой задачи,
так как `assign_port` ищет задачу по имени в реестре.

Проверка payload-контракта задаётся типом реестра:
- RT-реестр: payload обязан удовлетворять `RtPayload`
- Non-RT реестр: payload обязан удовлетворять `Steppable`

`TaskWrapper` остаётся общим; граница RT/Non-RT проходит на уровне `registry.add(...)`.

---

## 7. seal()

`seal()` вызывается один раз. Повторный вызов — ошибка.
Предусловие: все зарегистрированные wrapper-ы уже прошли `init()`.

Внутри `seal()` шаги выполняются в строгом порядке:

```
1. Сортировка задач по приоритету
   компаратор: primary = priority (убывание)
               secondary (tiebreaker) = порядок registry.add() (возрастание)
        ↓
2. Присвоение task_index
   (signal_bit == task_index)
        ↓
3. Публикация ссылок в sealed-view
   (`wrapper_ref` и `hb_ref` становятся read-only)
        ↓
4. Построение сигнальной маски
        ↓
5. Проверка инвариантов (см. раздел 8)
        ↓
6. Перевод реестра в read-only
```

Если любой инвариант нарушен — `seal()` завершается с ошибкой.
Ошибка указывает конкретную сущность и причину: `"ch_temp: нет writer"`, а не `"ошибка конфигурации"`.
Все нарушения возвращаются списком, не только первое.

---

## 8. Инварианты, проверяемые при seal()

**Ownership и типы:**

| Инвариант | Условие |
|-----------|---------|
| Ownership EventChannel | WriterCount == 1, ReaderCount == 1 |
| Ownership StateChannel | WriterCount == 1, ReaderCount == N |
| Типовая корректность | тип T совпадает у writer и reader |
| Writer существует | writer task объявлена в реестре |
| Readers существуют | каждый reader task объявлен в реестре |

**Параметры задач:**

| Инвариант | Условие |
|-----------|---------|
| CoreId корректен | core_id < NUM_CORES |
| Periodic: период | period_ticks ≥ 1 |
| Timeout | если observable == true → timeout_ticks задан |
| Ёмкость маски | task_count ≤ SIGNAL_MASK_WIDTH |

**Портовый слой:**

| Инвариант | Условие |
|-----------|---------|
| Порт существует | порт в assign_port объявлен в Task(...) |
| Направление порта корректно | `assign_port(..., writer)` использует `OutPort`, `assign_port(..., reader)` использует `InPort` |
| Уникальность task_name | повторный registry.add() задачи с тем же task_name — ошибка |
| Уникальность канала | повторный registry.add() того же канала — ошибка |
| Канал полностью связан | канал зарегистрирован только если writer и readers заданы по правилам типа |

---

## 9. Полный пример init()

```cpp
void init() {
    // 1. Задачи
    task_sensor_desc = Task(
        task_name="SNSR",
        core_id=0, priority=5, period_ticks=100,
        observable=true, timeout_ticks=150,
        ports=[OUT_TEMP, OUT_PRESSURE]
    )
    task_ctrl_desc = Task(
        task_name="CTRL",
        core_id=1, priority=10, period_ticks=0,  // SignalDriven
        observable=false,
        ports=[IN_TEMP, IN_PRESSURE, OUT_CMD]
    )
    task_log_desc = Task(
        task_name="LOGR",
        core_id=1, priority=1, period_ticks=1000,
        observable=false,
        ports=[IN_CMD]
    )

    // 2. Wrapper-ы (без heartbeat в конструкторе)
    task_sensor_wrapper = TaskWrapper(task_sensor_payload)
    task_ctrl_wrapper = TaskWrapper(task_ctrl_payload)
    task_log_wrapper = TaskWrapper(task_log_payload)

    // 3. Каналы
    ch_temp = StateChannel<float>()
    ch_pressure = StateChannel<float>()
    ch_cmd = EventChannel<Command>()

    // 4. Регистрация задач вместе с wrapper (attach_hb вызывается внутри registry.add)
    registry.add(task_sensor_desc, task_sensor_wrapper)
    registry.add(task_ctrl_desc, task_ctrl_wrapper)
    registry.add(task_log_desc, task_log_wrapper)

    // 5. Связывание — читается как схема (задачи должны быть уже зарегистрированы)
    registry.assign_port("SNSR", "OUT_TEMP", ch_temp, writer)
    registry.assign_port("CTRL", "IN_TEMP",  ch_temp, reader)

    registry.assign_port("SNSR", "OUT_PRESSURE", ch_pressure, writer)
    registry.assign_port("CTRL", "IN_PRESSURE",  ch_pressure, reader)

    registry.assign_port("CTRL", "OUT_CMD", ch_cmd, writer)
    registry.assign_port("LOGR", "IN_CMD",  ch_cmd, reader)

    // 6. Регистрация каналов
    registry.add(ch_temp)
    registry.add(ch_pressure)
    registry.add(ch_cmd)

    // 7. Инициализация wrapper-ов (до seal)
    task_sensor_wrapper.init()
    task_ctrl_wrapper.init()
    task_log_wrapper.init()

    // 8. Заморозка
    registry.seal()
}
```

---

## 10. Жизненный цикл реестра

| Фаза | Состояние | Что разрешено |
|------|-----------|---------------|
| pre-seal | открытый, изменяемый | add, remove, assign_port, wrapper.init |
| seal() | переход | сортировка → task_index → publish refs → маска → проверки → freeze |
| post-seal | frozen, read-only | только чтение; runtime использует реестр |

---

## 11. Что разработчик НЕ делает в реестре

- Не задаёт `signal_bit` и `task_index` вручную
- Не указывает intra/inter-core класс канала
- Не вычисляет и не оптимизирует приоритеты
- Не пишет scheduling-логику
- Не использует миллисекунды — только ticks
- Не добавляет dynamic/runtime/discovery механизмы (это v2)

---

## 12. Порядок действий при добавлении задачи или канала

```
1. Убедиться, что изменение не выходит за рамки v1
2. Создать Task(...) или Channel с полным набором параметров
3. Добавить `assign_port(...)` если создан новый канал
4. Вызвать registry.add(...)
5. Запустить init() + seal() — убедиться, что все инварианты зелёные
6. Проверить registry_report: изменение соответствует ожидаемому
```

---

## 13. Практика команды

**registry_report**
После `seal()` реестр выдаёт человекочитаемый отчёт в фиксированном порядке секций:

```
1. tasks          — в порядке task_index (возрастание)
                    поля: task_index, task_name, registration_order, priority, core_id, period_ticks, observable, scheduler_domain
2. channels       — в порядке registry.add() (фиксированно)
                    поля: name, тип<T>, writer (с именем порта), readers (с именами портов)
3. signal_mask    — итоговая маска в hex и побитовой раскладке
```

Формат секций стабилен между ревизиями — это условие корректного CI-diff.
Отчёт хранится как build-артефакт. Diff отчёта между ревизиями — обязательная часть ревью.

**Breaking changes**
Любое изменение, влияющее на `task_index` или состав сигнальной маски,
помечается как `breaking: registry layout`. К PR прикладывается diff `registry_report`.

**CI**
Шаг `registry init + seal` выполняется на каждый PR.
Ошибка `seal()` блокирует слияние.

---

*STAM System Registry v1 — Working Technology*
*Составлено по итогам обсуждения: оператор, Клод (Claude Sonnet 4.6), Codex (GPT-5)*
