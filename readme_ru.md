# Separated Task Application Model (STAM)

> **Статус:** активная R&D · contracts-first · Лицензия: MIT

STAM — это C++ архитектура исполнения и компонентная модель для детерминированных RT-систем.
Это **не** замена RTOS-ядра.

STAM определяет, как строить приложения из RT и non-RT компонентов, связанных явными каналами с формальными контрактами.

## Почему не просто FreeRTOS

FreeRTOS предоставляет планировщик, примитивы синхронизации и очереди.
Он не определяет архитектуру приложения.

Типичное RTOS-приложение со временем превращается в это:

```
ISR → очередь → задача → mutex → shared state → ещё мьютексы → timing chaos
```

Проблемы: неявная модель памяти, неконтролируемый jitter, нет формальных гарантий прогресса,
RT и non-RT логика перемешана в одних задачах.

STAM — ответ на эту проблему. Он определяет:

- **явное разделение доменов** — RT и non-RT ответственности разделены архитектурой и закреплены границами компонентов
- **семантические каналы** — IPC не контейнер, а объект с моделью потоков, memory ordering и контрактом прогресса
- **контракты вместо соглашений** — happens-before, misuse-сценарии и инварианты специфицируются, а не подразумеваются

## Как строить системы со STAM

1. Декомпозируй задачу на процессы, процессы — на подпроцессы
2. Определи **точки управления** — напиши RT-контроллер для каждой
3. Определи **источники данных** (датчики, входы) — оберни каждый как компонент
4. Свяжи всё типизированными каналами
5. Non-RT сторона (логирование, аналитика, конфигурация) читает из тех же каналов

Результат: система, в которой каждый путь передачи данных имеет определённый контракт,
а каждая граница домена явна.

## Для чего STAM

Встраиваемые и промышленные системы управления, где:

- требуется детерминированное RT-поведение
- RT и non-RT ответственности должны быть строго разделены
- семантика коммуникации должна быть явной и анализируемой

## Целевые платформы

| Архитектура | Примеры |
|---|---|
| ARM Cortex-M | STM32 и аналоги |
| ARM Cortex-A | embedded Linux, Raspberry Pi |
| x86/x64 | PC / промышленные контроллеры |

## Среды исполнения

STAM может работать:

- на bare metal
- поверх RTOS
- на SMP-системах

## Ключевое архитектурное правило

Два домена разделены по дизайну.

**RT-домен**
- ограниченное детерминированное исполнение
- без аллокаций
- без syscalls
- без блокировок
- без locks
- без IO
- явные инварианты и контракты

**non-RT-домен**
- персистентность
- логирование
- аналитика
- диагностика
- конфигурация
- всё дорогое и потенциально непредсказуемое

## Модель коммуникации

Каналы типизированы по семантике, а не по реализации.
Каждый вариант существует в форме UP (single-core / same-core) и SMP-safe:

| Семантика | UP | SMP-safe |
|---|---|---|
| Поток событий | `SPSCRing` | `SPSCRing` |
| Публикация снимка состояния | `DoubleBuffer`, `Mailbox2Slot` | `DoubleBufferSeqLock`, `Mailbox2SlotSmp` |
| Снимок для нескольких читателей | `SPMCSnapshot` | `SPMCSnapshotSmp` |

UP-варианты рассчитаны на исполнение на одном ядре или под контролем вытеснения.
SMP-варианты используют seqlock / claim-verify паттерны и безопасны между ядрами.

Каждый канал несёт явный контракт:

- модель потоков
- memory ordering (happens-before, точки линеаризации)
- гарантии прогресса (wait-free / bounded completion)
- misuse-сценарии и UB-модель
- бюджет RT-пути

Документация контрактов: [`primitives/docs/`](primitives/docs/)

## Структура репозитория

```
stam/
├── primitives/          # RT-safe lock-free IPC примитивы
│   ├── include/stam/
│   │   ├── primitives/  # SPSCRing, DoubleBuffer, SPMCSnapshot, Mailbox2Slot, CRC32_RT
│   │   └── sys/         # portability layer (arch, compiler, fence, preemption)
│   └── tests/
├── stam-rt-lib/         # RT execution model (TaskWrapper, SysRegistry)
│   └── include/
│       ├── exec/tasks/  # taskwrapper.hpp
│       └── model/       # tags.hpp (концепты: Steppable, RtPayload, ...)
├── modules/             # non-RT инфраструктура (logging, persistence и т.д.)
├── apps/
│   ├── brewery/         # Референсное приложение: RT control + non-RT logging
│   ├── demo/trivial_tasks/  # Минимальный пример RT/non-RT взаимодействия
│   └── minimal/         # Минимальный пример запуска
└── docs/
```

Жёсткое правило: `primitives/` и `stam-rt-lib/` не зависят от `modules/`. Обратная зависимость допустима.

## Архитектурные слои

**RT-примитивы** (`primitives/`): lock-free примитивы коммуникации со строгими RT-контрактами.
Без аллокаций, без locks, O(1) операции, acquire/release в рамках C++ memory model.

**Модель исполнения** (`stam-rt-lib/`): оркестрация RT-задач и абстракции планировщика.
Определяет, как задачи регистрируются, планируются и наблюдаются.

**non-RT модули** (`modules/`): дренаж RT-ring → staging queue / in-memory WAL → sink (file/flash/uart).
Политики надёжности хранения, аналитика, диагностика, конфигурация.

## Референсное приложение: Brewery Control

RT-цикл: датчики температуры → PID/bang-bang управление → исполнительные механизмы heater/cooler/pump/valve
→ телеметрия snapshot + lossy event log → non-RT.

Покрываемые safety-сценарии: перегрев, dry-run, отказ датчика, watchdog timeout,
escalation FSM (WARN → LIMIT → SHED → PANIC).

Non-RT сторона: логирование, персистентность, конфигурация/калибровка, экспорт метрик, анализ трендов.

## Модель безопасности

**Safety contract** определяет: safe state, fault model, escalation FSM, ownership safety-линий
(без re-entrancy), стратегию watchdog (internal + external), shutdown policy.

**Degradation contract** определяет: какие события отбрасываются первыми, какие *никогда* не отбрасываются
(PANIC/SAFETY), и условия восстановления.

## Принципы реализации (non-negotiable)

**RT-путь:** `noexcept`, без malloc/new/delete, без syscalls/IO/sockets, без locks/waits/condvars.
Ошибки выражаются через return values или счётчики.

**Portability:** единая кодовая база; конфигурация через `#define` / optional override header
(`user_sys_config.hpp`). CPU fences и cache maintenance — opt-in, задокументированы.

**Observability:** телеметрия через snapshot-каналы, события через lossy queue (`SPSCRing`),
уровень деградации как явный сигнал + метрики.

## Дорожная карта

- [x] Core RT примитивы + portability layer
- [ ] RT logger publish API + non-RT drain/sink pipeline
- [ ] Brewery RT domain skeleton (sensors → control → actuators → log)
- [ ] Safety + degradation contracts (docs + code)
- [ ] PC/Linux симуляция + unit/property тесты
- [ ] Портирование на STM32

## Быстрый старт

Единый задокументированный flow пока не оформлен.
Промежуточные точки входа:

- `apps/minimal` — базовый запуск
- `apps/demo/trivial_tasks` — минимальное RT/non-RT взаимодействие
- `apps/brewery` — полный референсный сценарий

---


> Если честно — всё началось с пивоварни. Нужно было не пережарить солод. Архитектура пришла позже, contracts-first — сам собой.
