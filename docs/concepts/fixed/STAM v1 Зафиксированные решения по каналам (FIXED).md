# FIXED решения по каналам (STAM v1)

Файл ведется только добавлением записей.

---

## [FIXED-CHAN-001] Базовый workflow привязки портов

- Для v1 базовый подход: `slot + assign_port` в bootstrap-фазе.
- Payload задает ожидаемые порты (имя/тип/направление).
- Система выполняет `task.assign_port(...)` до `seal()`.
- После `seal()` любые `assign/rebind` запрещены.

Источник консенсуса: `structure_thinkings.md`, сессии `00006`, `00007`, `00013`, `00017`.

## [FIXED-CHAN-002] Природа портов

- `InPort/OutPort` трактуются как `reference-wrapper`/handle, не как буфер.
- Порт не владеет хранилищем канала.
- Порты non-copyable в v1.
- Attach выполняется в bind/bootstrap, до `seal()`.

Источник консенсуса: `structure_thinkings.md`, сессии `00004`, `00007`, `00013`, `00017`.

## [FIXED-CHAN-003] Контракт EventChannel

- Публичный контракт v1: `EventChannel<T, Capacity, DropPolicy>`.
- `DropPolicy` относится к каналу/примитиву, а не к payload-порту.
- `read() -> false/no-data` является штатным результатом при отсутствии данных.

Источник консенсуса: `structure_thinkings.md`, сессии `00006`, `00007`, `00013`, `00017`.

## [FIXED-CHAN-004] Контракт StateChannel по данным

- Для SMP допускается состояние `unpublished`.
- До первого `publish` reader может получить `false/no-data`.
- Обязательный `initial_value` не является обязательным правилом v1.

Источник консенсуса: `structure_thinkings.md`, сессии `00012`, `00013`, `00017`.

## [FIXED-CHAN-005] Контракт StateChannel по размерности readers

- `StateChannel<T, N>`: `N` трактуется как точное число reader-задач.
- Проверка в `seal()`:
- `writer_bound == 1`
- `readers_bound == N`
- Несоответствие дает `SealError` с указанием канала и причины.

Источник консенсуса: `structure_thinkings.md`, сессии `00016`, `00013`, `00017`.

## [FIXED-CHAN-006] Полная привязка каналов на seal()

- `seal()` валидирует полную привязку портов всех каналов.
- Для `EventChannel<T, C, P>`: `writers==1`, `readers==1`.
- Для `StateChannel<T, N>`: `writers==1`, `readers==N`.
- Непривязанные порты являются ошибкой конфигурации и блокируют успешный `seal()`.

Источник консенсуса: `structure_thinkings.md`, сессии `00016`, `00013`, `00017`.

## [FIXED-CHAN-007] Intra/inter реализация

- Для v1 используется единая SMP-ready реализация примитивов.
- Класс `intra-core/inter-core` остается производной аннотацией реестра.
- Топология не переключает отдельную реализацию канала в v1.

Источник консенсуса: `structure_thinkings.md`, сессии `00004`, `00005`, `00013`, `00017`.
