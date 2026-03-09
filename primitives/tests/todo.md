# TODO по тестам примитивов

## Цель

Привести наборы тестов примитивов к понятной структуре:

- `contract tests` - проверяют только то, что обещано публичным контрактом
- `implementation tests` - проверяют текущие внутренние решения и layout
- `diagnostic stress tests` - собирают эмпирические данные, не требуя от кода больше, чем обещано контрактом

## DoubleBuffer

### Соответствие контракту

- Провести аудит [dbl_buffer_test.cpp](/src/primitives/tests/dbl_buffer_test.cpp) и разделить тесты по категориям.
- Оставить как contract tests:
  - требование trivially-copyable для `T`
  - lock-free `published`
  - начальное состояние `published == 0`
  - `read()` до первого `write()` возвращает zero-initialized `T`
  - `write()` затем `read()`
  - поведение latest-wins
  - повторные чтения стабильного состояния
  - большой trivially-copyable payload
  - fail-fast при повторном вызове `writer()` / `reader()`
- Переклассифицировать как non-contract:
  - тест строгого чередования слотов
  - cache-line / layout тесты
  - stress-тесты с эмпирическим измерением torn-rate

### Stress / диагностика

- Оставить метрики `torn/read` только как диагностические.
- Не требовать `torn == 0` в тестах с тяжёлым overlap: контракт этого не обещает.
- Подумать, не отделить ли диагностические stress-тесты от основного contract-suite хотя бы на уровне вывода.

### Не покрыто или покрыто слабо

- Решить, нужны ли отдельные тесты, прямо отражающие документацию:
  - "это не queue / промежуточные состояния могут теряться"
  - "consumer не влияет на producer" на уровне API / поведения
- Помнить, что свойства progress-типа (`wait-free`, `bounded O(1)`) в основном проверяются ревью кода и самим design-contract, а не unit-тестами.

## Следующие примитивы для аудита

- `Mailbox2Slot`
- `SPMCSnapshot`
- `SPMCSnapshotSmp`
- `DoubleBufferSeqLock`
- `Mailbox2SlotSmp`
- `SPSCRing`

## DoubleBufferSeqLock (статус)

- Тесты разделены по секциям:
  - `contract: static / compile-time`
  - `contract: behavior`
  - `implementation`
  - `diagnostic stress`
- Диагностический sustained stress вынесен под флаг:
  - CLI: `--diag-stress`
  - ENV: `STAM_TEST_DIAG_STRESS=1`
- Guard выдачи handle (`writer()` / `reader()`) переведены на атомики (`compare_exchange`).
- В контракте и header зафиксировано:
  - продуктовый профиль по умолчанию: `platform-optimized`
  - `strict` профиль в текущем классе не реализован.

Открыто:

- Если потребуется `strict` профиль, нужна отдельная реализация с race-free payload-путём по ISO C++ memory model.

## Mailbox2Slot (статус)

- Тесты разделены по секциям:
  - `contract: static / compile-time`
  - `contract: behavior`
  - `implementation`
  - `diagnostic stress`
- Диагностический stress вынесен под флаг:
  - CLI: `--diag-stress`
  - ENV: `STAM_TEST_DIAG_STRESS=1`
- Guard выдачи handle (`writer()` / `reader()`) переведены на атомики (`compare_exchange`).
- Обновлены ссылки на актуальный контрактный документ в тесте и header.

Открыто:

- Проверить, нужны ли дополнительные contract-тесты на негативные сценарии протокола claim/verify (помимо текущих stress-метрик).
