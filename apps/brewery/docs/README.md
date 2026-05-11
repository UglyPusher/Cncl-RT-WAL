---
doc_id: BREWERY_DOCS_INDEX
title: Brewery Controller Docs Index
status: draft
version: 1.0
depends_on:
  - SYS_INTENT_AUTOMATION_BOUNDARY@1.1
  - BREWERY_V1_PLAN@1.0
  - BREWERY_WIRING@1.0
  - RECIPE_SPEC_V1@1.0
  - SAFETY_CONTROLLER_V1@1.0
  - SYSTEM_DOWN@1.0
  - BREWERY_DOCS_TODO@1.0
  - BREWERY_PLAN_QL_REFERENCE@0.1
  - BREWERY_PLAN_LEGACY@0.1
  - CHANNEL_WIRING_LEGACY@0.1
  - TASK_CHANNEL_LIST_LEGACY@0.1
  - HW_SCHEME_LEGACY@0.1
  - SW_SCHEME_LEGACY@0.1
review_scope:
  - document_index
  - canonical_sources
  - legacy_status
  - update_rules
---

# Brewery Controller Docs

Документация описывает v1 контроллера пивоварни на базе STAM:
основной контроллер STM32F103, независимый safety-контроллер ATtiny3216,
лок-фри обмен между задачами, рецепты, safety-инварианты и аппаратную
топологию.

## Цель работы

- Проверить STAM на практической задаче с RT/Non-RT задачами.
- Получить контроллер для замены штатного контроллера Bavaria/Speidel Braumeister.
- Довести спецификации до уровня, где из них можно генерировать структуру ПО:
  задачи, каналы, payload-структуры, HAL-интерфейсы и проверки целостности.

## Канонические документы v1

- `Sys_Intent_Automation_Boundary_Draft_v1.md` — верхнеуровневый контракт назначения системы, границ автоматизации, safety-authority и energy domains (`doc_id: SYS_INTENT_AUTOMATION_BOUNDARY`, status finalized, version 1.1).
- `plan_v.1.md` — основной предпроект v1: scope, safety, HAL, backlog (`doc_id: BREWERY_V1_PLAN`, status draft, version 1.0).
- `brewery-wiring.yaml` — каноническая машинно-читаемая модель задач и каналов (`doc_id: BREWERY_WIRING`, status draft, version 1.0).
- `recipe_spec_v1.md` — модель рецепта v1, валидация, хранение и UI-сценарии (`doc_id: RECIPE_SPEC_V1`, status draft, version 1.0).
- `safety_controller_spec_v1.md` — поведение независимого safety-контроллера v1; текущая реализация — ATtiny3216 (`doc_id: SAFETY_CONTROLLER_V1`, status draft, version 1.0).
- `system_down_addition.md` — инвариант безусловного отключения исполнительных устройств (`doc_id: SYSTEM_DOWN`, status draft, version 1.0).
- `TODO.md` — список недостающих формальных контрактов и работ по выравниванию (`doc_id: BREWERY_DOCS_TODO`, status draft, version 1.0).

## Reference / Legacy

- `plan_ql.md` — расширенная проектная записка. Использовать как пояснение, но решения v1 сверять с `plan_v.1.md` (`doc_id: BREWERY_PLAN_QL_REFERENCE`, status reference, version 0.1).
- `plan.md` — deprecated исторический черновик (`doc_id: BREWERY_PLAN_LEGACY`, status deprecated, version 0.1).
- `channel-wiring.md` — legacy/reference по ранним вариантам wiring; актуальная модель в `brewery-wiring.yaml` (`doc_id: CHANNEL_WIRING_LEGACY`, status reference, version 0.1).
- `task-channel_list.md` — legacy/reference по раннему списку задач; актуальная модель в `brewery-wiring.yaml` (`doc_id: TASK_CHANNEL_LIST_LEGACY`, status reference, version 0.1).
- `hw_scheme.md` — ранний аппаратный черновик, требует выравнивания под ATtiny3216 (`doc_id: HW_SCHEME_LEGACY`, status deprecated, version 0.1).
- `sw_scheme.md` — ранний набросок состояний, требует выравнивания с v1 FSM (`doc_id: SW_SCHEME_LEGACY`, status reference, version 0.1).

## Принятые решения v1

- Система является semi-automatic deterministic process controller, а не автономной системой варки или сертифицированной safety-system.
- Firmware не является окончательной safety authority; dangerous energy должна сниматься независимо от состояния firmware.
- Safety MCU: ATtiny3216.
- STM32 -> ATtiny3216: однонаправленный USART-кадр состояния с CRC.
- ATtiny3216 управляет только контактором силовой части.
- `SYSTEM_DOWN()` является глобальным примитивом safe-state, а не задачей и не каналом.
- `SYSTEM_DOWN()` не заменяет ATtiny3216, контактор, E-Stop или аппаратные cutoff devices.
- Система разделяет минимум `CONTROL_POWER_DOMAIN` и `ACTUATOR_POWER_DOMAIN`; удаление dangerous energy выполняется через отключение actuator-домена независимыми safety mechanisms.
- Нагрев v1 управляется гистерезисным регулятором, без PID.
- Ширина гистерезиса нагрева: 0.5 °C вокруг уставки.
- Модель задач и каналов берётся из `brewery-wiring.yaml`.
- Жёсткие ограничения v1 перечислены в `plan_v.1.md`: без PID,
  adaptive control, multiple temp arbitration, smart flow regulation,
  dynamic task graph, runtime safety configuration, bidirectional MCU protocol
  и distributed consensus между МК.

## Оглавление по темам

- Назначение системы и границы автоматизации: `Sys_Intent_Automation_Boundary_Draft_v1.md`
- Архитектура v1: `plan_v.1.md`
- Wiring задач и каналов: `brewery-wiring.yaml`
- Рецепты: `recipe_spec_v1.md`
- Safety controller: `safety_controller_spec_v1.md`
- Безусловное отключение: `system_down_addition.md`
- Открытые работы: `TODO.md`

## Правила обновления

- Новые решения сначала фиксировать в каноническом документе.
- Верхнеуровневые изменения intent/scope/automation boundary фиксировать в `Sys_Intent_Automation_Boundary_Draft_v1.md`.
- Если решение влияет на структуру ПО, обновлять `brewery-wiring.yaml`.
- В `depends_on` указывать только существующие документы с указанными `doc_id` и `version`.
- Legacy-файлы не считать источником истины без явной миграции в canonical docs.
- Safety-решения должны быть отражены минимум в `plan_v.1.md`, `TODO.md` или отдельной safety-спеке.
