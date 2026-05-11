---
doc_id: BREWERY_DOCS_TODO
title: Brewery Documentation TODO
status: draft
version: 1.0
depends_on:
  - SYS_INTENT_AUTOMATION_BOUNDARY@1.1
  - BREWERY_V1_PLAN@1.0
  - BREWERY_WIRING@1.0
  - RECIPE_SPEC_V1@1.0
  - SAFETY_CONTROLLER_V1@1.0
  - SYSTEM_DOWN@1.0
review_scope:
  - blocking_specs
  - document_alignment
  - safety_open_items
---

# Brewery Docs TODO

Список работ по доведению документации до состояния, пригодного для реализации
и генерации структуры ПО.

## Блокирующие спецификации

- [ ] Финальная спека кадра `STM32 -> ATtiny3216`.
  - Зафиксировать версию кадра, период отправки, timeout, endian, длину, поля и масштабирование температур.
  - Зафиксировать CRC-алгоритм, начальное значение, порядок байт и тест-векторы.
  - Зафиксировать политику единичных CRC-ошибок и `CRC_FAIL_LIMIT`.

- [ ] Полный HAL-контракт для STM32 и ATtiny3216.
  - Перечень интерфейсов и методов.
  - Для каждого метода: RT-safe/Non-RT-safe, ISR-safe, bounded execution, возможные ошибки.
  - Контракт времени: `TickSource`, `TICK_HZ`, wrap-around, атомарность чтения.
  - Контракт тестового HAL под Linux/POSIX.

- [ ] Формализация `SYSTEM_DOWN()`.
  - Связать reason codes, порядок действий, post-mortem обработчик, логирование и UI ack.
  - Зафиксировать допустимые контексты вызова.
  - Зафиксировать, какие HAL-операции входят в безусловный safe-state.

- [ ] Формализация `recipe_spec_v1.md`.
  - Перевести logical model в C/C++ структуры или schema-описание.
  - Зафиксировать бинарный layout, packing/alignment, versioning и CRC.
  - Добавить тестовые рецепты и негативные test vectors.

## Выравнивание документов

- [ ] Сделать `brewery-wiring.yaml` единственным источником модели задач и каналов.
- [x] Убрать или явно пометить `scope: v2/out_of_scope` для flow sensor и независимого temperature sensor на ATtiny3216.
- [ ] Свести старые reference-файлы `channel-wiring.md` и `task-channel_list.md` к `brewery-wiring.yaml` или удалить после миграции.
- [x] Обновить `hw_scheme.md` под ATtiny3216 или пометить как legacy.

## Safety

- [ ] Единая таблица safety/interlocks v1: condition -> action -> reason -> log -> ack -> latch/reset.
- [ ] Единый список reason codes STM32 и ATtiny3216.
- [ ] Формат статусного регистра ATtiny3216 и протокол чтения по GPIO.
