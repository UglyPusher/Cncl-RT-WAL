# Separated Task Application Model (STAM)

> **Статус:** активная R&D · contracts-first · Лицензия: MIT

C++ библиотека RT и non-RT компонентов для embedded/industrial систем, построенная вокруг строгого разделения доменов и явных контрактов. Включает референсное приложение *brewery control* как демонстрацию реального сценария.

**Ключевой принцип:**
- **RT-домен** — bounded, deterministic, no-alloc, no syscalls, no blocking.
- **non-RT-домен** — персистентность, аналитика, диагностика; всё дорогое и потенциально непредсказуемое.

---

## Целевые платформы

| Архитектура | Примеры |
|---|---|
| ARM Cortex-M | STM32 и др. |
| ARM Cortex-A | embedded Linux, Raspberry Pi |
| x86/x64 | PC / industrial controllers |

---

## Структура репозитория

```
stam/
├── primitives/          # RT-safe lock-free primitives (SPSCRing, DoubleBuffer, …)
│   ├── include/stam/
│   │   ├── primitives/  # SPSCRing, DoubleBuffer, SPMCSnapshot, Mailbox2Slot, CRC32_RT
│   │   └── sys/         # portability layer (arch, compiler, fence, preemption)
│   └── tests/
├── stam-rt-lib/         # RT execution model (TaskWrapper, SysRegistry)
│   └── include/
│       ├── exec/tasks/  # taskwrapper.hpp
│       └── model/       # tags.hpp (concepts: Steppable, RtPayload, …)
├── modules/             # Non-RT components (logging, …)
├── apps/
│   ├── brewery/         # Reference application: RT control + non-RT logging
│   ├── demo/
│   │   └── trivial_tasks/ # Minimal RT/non-RT interaction demo
│   └── minimal/         # Minimal boot example
└── docs/
```

**Жесткое правило:** `primitives/` и `stam-rt-lib/` не зависят от `modules/`. Обратная зависимость допустима.

---

## Архитектура

### RT-компоненты (hard-RT путь)
- без аллокаций, без syscalls/IO, без locks/waits
- ограниченные O(1) операции
- acquire/release строго в рамках C++ memory model
- явные инварианты и misuse-контракты для каждого компонента

Примитивы: `SPSCRing`, `DoubleBuffer`, `SPMCSnapshot`, `Mailbox2Slot`, `CRC32_RT`

### Non-RT компоненты
- drain RT-ring → staging queue / in-memory WAL → sink (file/flash/uart)
- политики надежности хранения (fsync/flush, batching, backpressure)
- аналитика / диагностика / UI / симуляторы окружения

### HAL (Hardware Abstraction Layer)
Изолирует платформо-зависимый код: источник тиков, GPIO/ADC/SPI/I2C/UART, cache maintenance, watchdog, failsafe outputs. RT-код зависит от HAL только через минимальный интерфейс.

---

## Референсное приложение: Brewery Control

**RT-цикл:** температурные датчики → PID/bang-bang управление → исполнительные механизмы heater/cooler/pump/valve → snapshot телеметрии + lossy event log → non-RT.

**Покрываемые safety-сценарии:** перегрев, dry-run, отказ датчика, watchdog timeout, escalation FSM (WARN → LIMIT → SHED → PANIC).

**non-RT:** логирование, персистентность, конфиг/калибровка, экспорт метрик, анализ трендов.

---

## Контракты компонентов

Каждый примитив и компонент сопровождается формальным контрактом, включающим:

- Scope / semantic model
- Compile-time и runtime инварианты
- Предусловия модели потоков
- Memory ordering (happens-before), точки линеаризации
- Progress guarantees (wait-free / bounded completion)
- Misuse-сценарии и UB-модель
- Примечания по бюджетам RT-пути

Документация контрактов: [`primitives/docs/`](primitives/docs/)

---

## Модель безопасности

**Safety contract** определяет: safe state, fault model, escalation FSM, ownership safety-линий (без re-entrancy), стратегию watchdog (internal + external), shutdown policy.

**Degradation contract** определяет: какие события отбрасываются первыми, какие *никогда* не отбрасываются (PANIC/SAFETY), и условия восстановления.

---

## Принципы реализации (non-negotiable)

**RT path:** `noexcept`, без malloc/new/delete, без syscalls/IO/sockets, без locks/waits/condvars. Ошибки выражаются через return values или counters.

**Portability:** единая кодовая база; конфигурация через `#define` / optional override header (`user_sys_config.hpp`). CPU fences и cache maintenance включаются opt-in и документируются.

**Observability:** телеметрия через snapshot (`DoubleBuffer`), события через lossy queue (`SPSCRing`), уровень деградации как явный сигнал + метрики.

---

## Дорожная карта

- [x] Core RT primitives + portability layer
- [ ] RT logger publish API + non-RT drain/sink pipeline
- [ ] Brewery RT domain skeleton (sensors → control → actuators → log)
- [ ] Safety + degradation contracts (docs + code)
- [ ] PC/Linux simulation + unit/property tests
- [ ] Port to STM32

---

## Быстрый старт

Пока не оформлен как единый документированный flow.  
Промежуточные точки входа:
- `apps/minimal` для базового запуска
- `apps/demo/trivial_tasks` для RT/non-RT взаимодействия
- `apps/brewery` для референсного сценария
