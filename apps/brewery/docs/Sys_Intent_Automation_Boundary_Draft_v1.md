---
doc_id: SYS_INTENT_AUTOMATION_BOUNDARY
title: System Intent & Automation Boundary
status: finalized
version: 1.1
depends_on: []
review_scope:
  - terminology
  - automation_boundary
  - guarantees
  - safety_authority
  - energy_domains
  - failure_modes
---

# Представление системы и границ автоматизации

## STAM Brewery Controller — System Intent & Automation Boundary (Draft v1)

---

# 1. Назначение системы

Система предназначена для управления малой и средней пивоварней периодического действия (batch brewing system) с электрическим нагревом и рециркуляцией сусла.

Цель системы:

* автоматизация повторяемых операций;
* поддержание температурных режимов;
* координация исполнительных устройств;
* контроль базовых interlock-состояний;
* обеспечение deterministic safe-state behavior при отказах.

Система НЕ является:

* автономной системой варки пива;
* интеллектуальной системой принятия технологических решений;
* сертифицированной safety-system;
* заменой квалифицированного оператора.

---

# 2. Класс системы

Система относится к классу:

```text
Semi-automatic deterministic process controller
with independent hardware safety layers.
```

Система управляет:

* нагревателями;
* помпами;
* технологическими таймерами;
* переходами между стадиями процесса.

Система не принимает решений о:

* качестве рецепта;
* качестве сырья;
* корректности технологического процесса;
* пригодности продукта;
* технологической допустимости действий оператора вне формализованных interlock-условий.

---

# 3. Границы автоматизации

## Система автоматизирует

* поддержание температуры;
* выполнение последовательности стадий рецепта;
* управление помпами;
* технологические таймеры;
* interlock-логику;
* software-requested emergency stop через SYSTEM_DOWN();
* переход в safe-state;
* журналирование критических событий.

---

## Система НЕ автоматизирует

* внесение ингредиентов;
* контроль пенообразования;
* контроль качества кипа;
* водоподготовку;
* санитарную обработку;
* CIP;
* ферментацию (если не реализован отдельный профиль системы);
* технологическую корректность рецепта;
* диагностику качества продукта;
* контроль физического состояния оборудования вне доступных датчиков.

---

# 4. Роль оператора (пивовара)

Оператор остаётся ответственным за:

* запуск системы;
* контроль наличия жидкости;
* санитарное состояние оборудования;
* корректность рецепта;
* контроль процесса кипячения;
* физическое состояние оборудования;
* проверку датчиков и исполнительных устройств;
* подтверждение аварийных состояний;
* принятие технологических решений.

Система не гарантирует получение пригодного или качественного продукта при ошибках оператора или нарушении технологии.

---

# 5. Роль firmware

Firmware отвечает за:

* deterministic control behavior;
* bounded reaction semantics;
* interlock processing;
* safe-state transitions;
* управление исполнительными устройствами;
* журналирование событий;
* coordination logic;
* RT/non-RT separation;
* соблюдение внутренних архитектурных контрактов.

Firmware не является окончательной safety authority.

Firmware может инициировать safe-state и отключать управляемые выходы со стороны STM32,
но не считается единственным механизмом удаления dangerous energy.

---

## Verification boundary

Firmware обязана обеспечивать разделение RT и Non-RT путей через:

* compile-time contracts;
* ограничения примитивов STAM;
* ограничения payload semantics;
* разделение HAL API по допустимому контексту вызова;
* static_assert-based platform validation;
* запрет блокирующих операций в RT paths.

RT guarantees считаются действительными только при соблюдении HAL bounded-time contracts.

Нарушение bounded-time contract:

* invalidates RT assumptions;
* переводит соответствующий execution path в undefined RT behavior;
* считается ошибкой platform/HAL implementation.

---

# 6. Safety philosophy

## Основной принцип

```text
Dangerous energy must be removable independently from firmware state.
```

Система предполагает, что:

* firmware может зависнуть;
* MCU может перейти в неопределённое состояние;
* периферия может отказать;
* датчики могут выдавать ложные значения;
* исполнительные устройства могут выйти из строя.

---

## Следствия

Безопасность системы не должна зависеть исключительно от:

* scheduler;
* UI;
* recipe logic;
* RT behavior;
* состояния STM32;
* корректности high-level software.

---

# 7. Независимые уровни защиты

## Уровень 1 — firmware safety (STM32)

Обеспечивает:

* программные interlock’и;
* behavioral checks;
* SYSTEM_DOWN();
* диагностику аномалий;
* safe-state со стороны firmware.

---

## Уровень 2 — independent safety controller (ATtiny3216)

ATtiny3216:

* не управляет рецептом;
* не принимает технологических решений;
* не выполняет model-based analysis;
* не дублирует основную логику STM32.

ATtiny3216 выполняет только:

* gating силовой части;
* контроль hard safety conditions;
* watchdog-based shutdown.

---

### Self-watchdog

ATtiny3216 использует внутренний hardware watchdog для контроля собственной liveliness.

Self-watchdog предназначен для детекта:

* deadlock;
* runaway execution;
* corrupted execution state;
* scheduler-free main-loop stall.

Срабатывание self-watchdog:

* приводит к hardware reset;
* обязано переводить контактор в OFF state;
* фиксируется как отдельная причина fault/reset.

---

### Peer supervision

ATtiny3216 контролирует liveliness STM32 через:

* freshness USART frame;
* frame timeout policy;
* CRC-valid frame semantics.

USART frame является:

* carrier of state;
* semantic heartbeat STM32.

Отдельный GPIO heartbeat не считается достаточным подтверждением корректной работы STM32.

---

## Уровень 3 — independent hardware protection

Аппаратные элементы:

* биметаллические термореле;
* thermal fuse;
* pressure switches;
* иные независимые hardware cutoff devices.

Работают независимо от firmware и MCU.

---

## Уровень 4 — Emergency Stop

E-Stop:

* физически снимает dangerous energy минимум с ACTUATOR_POWER_DOMAIN;
* может также снимать CONTROL_POWER_DOMAIN, если так реализована конкретная схема;
* не зависит от firmware;
* является последним механизмом аварийного останова.

---

# 8. Energy domains

Система разделяется минимум на два power domains:

## CONTROL_POWER_DOMAIN

Питает:

* STM32;
* ATtiny3216;
* UI;
* low-voltage electronics.

Может оставаться под напряжением после safety trip.

---

## ACTUATOR_POWER_DOMAIN

Питает:

* нагреватели;
* помпы;
* исполнительные устройства.

Должен отключаться независимыми safety mechanisms.

Отключение ACTUATOR_POWER_DOMAIN является основным способом удаления dangerous energy.
CONTROL_POWER_DOMAIN может оставаться под напряжением только если это не сохраняет
возможность включения исполнительных устройств в обход independent safety mechanisms.

---

# 9. SYSTEM_DOWN() philosophy

SYSTEM_DOWN() является:

* глобальным safe-state primitive;
* не задачей;
* не частью scheduler;
* не частью channel/dataflow model.

SYSTEM_DOWN() обязан:

* выполняться deterministic;
* быть idempotent;
* выполняться за bounded time;
* не зависеть от channel topology;
* не требовать scheduler liveness;
* не заменять ATtiny3216, контактор, E-Stop или аппаратные cutoff devices.

---

## Требования к degraded-context execution

SYSTEM_DOWN() обязан быть корректно вызываем:

* из RT context;
* из ISR context;
* при частичном разрушении scheduler state;
* при повторной fault escalation;
* при nested emergency conditions.

SYSTEM_DOWN() не должен зависеть от:

* scheduler progress;
* lock ownership;
* allocator state;
* completion of pending tasks;
* interrupt delivery.

---

## Reentrancy semantics

SYSTEM_DOWN() не обязан поддерживать true concurrent execution semantics.

Однако SYSTEM_DOWN() обязан обеспечивать:

* safe repeated invocation;
* safe nested invocation;
* idempotent shutdown semantics;
* deterministic repeated execution result.

---

# 10. RT philosophy

RT guarantees существуют только для:

* явно определённых RT paths;
* задач с зафиксированными bounded contracts;
* HAL-операций с формализованным bounded behavior.

Любой HAL operation:

* с блокировкой;
* ожиданием;
* неопределённым временем выполнения;

нарушает RT assumptions.

---

# 11. Hardware profile philosophy

Система использует:

```text
fixed hardware profiles
```

Система НЕ использует:

* runtime topology discovery;
* dynamic hardware composition;
* runtime feature negotiation.

Каждый hardware profile:

* имеет фиксированную topology;
* фиксированные channels;
* фиксированные safety assumptions;
* отдельную verification matrix;
* отдельный firmware build.

---

## Hardware/Firmware identity

Каждый firmware build обязан содержать:

* machine profile identifier;
* hardware revision identifier;
* firmware build identifier.

Firmware profile обязан быть machine-specific.

---

## Startup profile validation

Система обязана выполнять проверку совместимости:

* firmware profile;
* hardware profile;
* board revision.

Несовпадение profile identity:

* запрещает переход в active operating modes;
* переводит систему в safe-state;
* фиксируется как configuration fault.

---

## Допустимые механизмы hardware identification

Допускаются:

* resistor ladder identification;
* GPIO straps;
* EEPROM/FRAM identity;
* board revision pins;
* иные deterministic hardware identity mechanisms.

---

# 12. Behavioral verification philosophy

Система может использовать:

* behavioral checks;
* thermal response analysis;
* electrical feedback;
* flow verification;
* stale-data analysis.

Но:

* сложная диагностика принадлежит STM32;
* independent safety controller не выполняет model reasoning.

---

# 13. Out of scope

Из области проекта исключаются:

* adaptive control;
* autonomous brewing;
* AI/ML decision systems;
* dynamic runtime configuration;
* automatic process optimization;
* automatic recipe correction;
* distributed controller consensus;
* self-modifying topology;
* generalized universal appliance framework.

---

# 14. Failure philosophy

Основной принцип:

```text
Failure is expected.
Dangerous behavior is not acceptable.
```

Система должна:

* переходить в safe-state при неопределённости;
* требовать ручного восстановления после critical faults;
* сохранять deterministic shutdown behavior.

---

# 15. Responsibility boundaries

## Firmware developer responsibility

* deterministic behavior;
* interlock semantics;
* safe-state semantics;
* bounded execution assumptions;
* architectural contracts.

---

## Integrator responsibility

* electrical wiring;
* grounding;
* breaker selection;
* contactor selection;
* thermal installation;
* EMC compatibility;
* actuator compatibility;
* PSU stability;
* compliance with local regulations.

---

## Operator responsibility

* технологический процесс;
* санитария;
* наличие жидкости;
* контроль кипа;
* проверка оборудования;
* реакция на alarms/interlocks.

---

# 16. Non-certification statement

Проект:

* не является сертифицированной safety-system;
* не заменяет industrial safety engineering;
* не гарантирует безопасность при произвольной модификации hardware/software;
* требует независимых hardware safety mechanisms.

Использование системы с опасными уровнями энергии требует отдельной инженерной валидации.

---

# 17. Architectural invariants

Следующие положения считаются фундаментальными инвариантами системы:

* Firmware is not the final safety authority.
* Dangerous energy must be removable independently from firmware.
* SYSTEM_DOWN() is outside scheduler/dataflow semantics.
* Hardware topology is fixed per firmware profile.
* Independent safety logic must remain simple and deterministic.
* Safety must degrade toward de-energized state.
* RT paths must remain bounded and analyzable.
* Human operator remains part of the control loop.
* Semantic heartbeat is preferred over raw GPIO heartbeat.
* Independent safety controller must not perform complex behavioral reasoning.
* RT guarantees require validated HAL bounded-time behavior.
* Hardware/Firmware profile mismatch must prevent active operation.
* SYSTEM_DOWN() must remain callable from degraded execution contexts.
