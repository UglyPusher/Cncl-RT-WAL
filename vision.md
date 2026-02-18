# RT-WAL Component Library

**Назначение:** библиотека RT и non-RT компонентов для embedded/industrial систем с явными контрактами (RT, safety, portability) + reference-application “brewery control” как демонстрация реального проектного мышления.

Проект строится вокруг принципа:

> **RT-домен** — bounded, deterministic, no-alloc, no syscalls, no blocking.  
> **non-RT-домен** — персистентность, аналитика, диагностика, всё “дорогое” и потенциально непредсказуемое.

---

## 0. Что является “готовым результатом”

### MIN (репа-демо)
1) **Separated Task Application Model (STAM)**: набор формально описанных примитивов и компонент для RT/non-RT границы.  
2) **Brewery showcase app**: код, демонстрирующий RT-контур управления + non-RT логирование/персистентность.  
3) **Контракты компонентов**: инварианты, misuse/UB-сценарии, progress guarantees, latency/jitter бюджеты.  
4) **Safety contracts**: safe state, fault model, escalation/degradation FSM (WARN→LIMIT→SHED→PANIC).  
5) **Portability layer**: конфигурация без правок исходников через макросы / user override.

### MAX (железо)
Собрать систему на референсном железе, измерить jitter/WCET/latency, продемонстрировать безопасные сценарии и деградацию.

---

## 1. Целевые платформы

- ARM Cortex-M (STM32 и др.)
- ARM Cortex-A (embedded Linux, Raspberry Pi и др.)
- x86/x64 (PC/industrial controllers)

---

## 2. Архитектура (жёсткое разделение доменов)

### 2.1 RT Components (hard-RT path)
- без аллокаций
- без syscalls/IO
- без блокировок/ожиданий
- bounded O(1) операции
- acquire/release строго по C++ memory model
- явные **инварианты и misuse-сценарии**

Типовые блоки:
- контроллеры (PID / bang-bang)
- детерминированные FSM
- safety line gating / watchdog hooks
- RT publish primitives: `SPSCRing`, `DoubleBuffer`, “shadow IO”
- RT-WAL record encode + CRC32C (без malloc)

### 2.2 Non-RT Components
- drain RT-ring → staging queue / in-memory WAL → sink (file/flash/uart)
- storage durability policies (fsync/flush, batching, backpressure)
- аналитика / диагностика / UI
- тестовые симуляторы окружения

### 2.3 HAL (Hardware Abstraction Layer)
Изоляция платформо-зависимого кода:
- tick source / timers
- GPIO/ADC/SPI/I2C/UART
- cache maintenance (если требуется)
- watchdog line
- power cut / failsafe outputs

RT-код зависит от HAL **по минимальному интерфейсу**, не от конкретной платформы.

---

## 3. Демонстрационное применение: Brewery Control (reference implementation)

### RT-контур (пример)
- датчики температуры (валидаторы/диагностика)
- управление нагревателем/охладителем/насосами/клапанами
- safety-критичные сценарии: перегрев, dry-run, отказ датчика, watchdog
- публикация телеметрии (snapshot) + событий (lossy log) в non-RT

### non-RT
- логирование и персистентность
- конфиг / калибровка
- мониторинг, экспорт метрик
- анализ трендов (не влияет на RT)

---

## 4. Safety: что именно считается “индустриальным”

### 4.1 Safety contract (системный)
Документ должен фиксировать:
- **safe state** (что значит “безопасно” для системы)
- **fault model** (что ломается и как детектируется)
- **escalation FSM**: WARN → LIMIT → SHED → PANIC
- **ownership** safety lines (кто владеет сигналом и как исключается re-entrancy)
- **watchdog strategy** (internal + external)
- **shutdown policy** (какой порядок остановки доменов)

### 4.2 Degradation contract (логирование/персист)
- какие уровни деградации существуют
- какие события выпадают первыми
- какие события **никогда не выпадают** (например, PANIC/SAFETY)
- как восстанавливаемся (если разрешено) и на каких условиях

---

## 5. Контракты компонентов (обязательная часть)

Для каждого ключевого примитива/компонента:
- Scope / semantic model
- Compile-time invariants
- Runtime invariants
- Threading model preconditions
- Memory ordering (happens-before)
- Linearization points (если применимо)
- Progress guarantees (wait-free/bounded completion)
- Misuse scenarios → UB модель компонента
- RT path budget notes (если есть)

---

## 6. Репозиторий: структура (предварительно)


**Жёсткое правило:** `include/rt` не включает `include/nonrt`. Обратно — можно.

---

## 7. Принципы реализации (non-negotiable)

1) **RT-path:**
   - `noexcept`
   - no malloc/new/delete
   - no syscalls / file IO / sockets
   - no locks / waits / condition variables
   - bounded time; отказ выражается return value / counters

2) **Portability:**
   - одна кодовая база, конфигурация через defines / optional override header
   - стандартная C++ memory model как baseline
   - CPU fences / cache maintenance — только opt-in и документировано

3) **Наблюдаемость:**
   - телеметрия — snapshot (DoubleBuffer)
   - события — lossy queue (SPSCRing)
   - уровень деградации — явный сигнал и метрики

---

## 8. Roadmap (ориентир, не “спринт”)

1) Core RT primitives + portability layer
2) RT logger publish API + non-RT drain/sink pipeline
3) Brewery RT domain skeleton (sensors→control→actuators→log)
4) Safety + degradation contracts (docs + код)
5) Симуляция PC/Linux + unit/property tests
6) Порт на STM32 (MAX этап)

---

## 9. Лицензия / статус

- Статус: active R&D (contracts-first).
- Лицензия: TBD.

---

## 10. Быстрый старт

Будет добавлен после появления:
- минимального `apps/brewery` (PC/Linux симуляция)
- базовых тестов `tests/unit`