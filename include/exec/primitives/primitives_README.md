# exec/primitives

Низкоуровневые RT-примитивы для межпоточной передачи данных.

Все компоненты разработаны под жёсткие требования реального времени:
wait-free операции, детерминированный WCET, без динамической памяти,
без исключений, без блокировок.

---

## Компоненты

### DoubleBuffer

Ping-pong снапшот-буфер (latest-wins). Передаёт последнее
опубликованное состояние от одного писателя к одному читателю.
`write()` всегда успешен, `read()` всегда возвращает значение.

| Файл | Документация |
|---|---|
| `dbl_buffer.hpp` | [`docs/contracts/DoubleBuffer — RT Contract & Invariants.md`](../../../docs/contracts/DoubleBuffer%20—%20RT%20Contract%20&%20Invariants.md) |

---

### Mailbox2Slot

SPSC снапшот-мейлбокс с семантикой latest-wins и протоколом
claim-verify. В отличие от `DoubleBuffer`, `try_read()` может
вернуть `false` при гонке публикации — reader остаётся на
предыдущем (sticky) состоянии. Без retry: промах = следующий тик.

| Файл | Документация |
|---|---|
| `mailbox2slot.hpp` | [`docs/contracts/Mailbox2Slot_v1.3 — RT Contract & Invariants.md`](../../../docs/contracts/Mailbox2Slot_v1.3%20—%20RT%20Contract%20&%20Invariants.md) |

---

### SPSCRing

Single-Producer Single-Consumer lock-free кольцевой буфер (FIFO).
Гарантирует доставку каждого элемента в порядке записи.
`push()` возвращает `false` при переполнении (без overwrite).
Ёмкость должна быть степенью двойки; usable = Capacity − 1.

| Файл | Документация |
|---|---|
| `spsc_ring.hpp` | [`docs/contracts/SPSCRing — RT Contract & Invariants.md`](../../../docs/contracts/SPSCRing%20—%20RT%20Contract%20&%20Invariants.md) |

---

### crc32_rt

CRC32C (Castagnoli) — инкрементальный и one-shot интерфейсы.
Таблица вычисляется на этапе компиляции (`constexpr`), runtime-инициализация
отсутствует. Корректность алгоритма подтверждается `static_assert`
с каноническим тест-вектором `"123456789"` → `0xE3069283`.

| Файл | Документация |
|---|---|
| `crc32_rt.hpp` | *(встроена в заголовок)* |

---

## Сравнение семантик

| Примитив | Семантика | Потеря данных | Блокировка | `push`/`write` при заполнении |
|---|---|---|---|---|
| `DoubleBuffer` | Снапшот / latest-wins | Промежуточные теряются | Нет | Всегда успешен (overwrite) |
| `Mailbox2Slot` | Снапшот / latest-wins | Промежуточные теряются | Нет (claim-verify) | Всегда успешен (overwrite) |
| `SPSCRing` | Очередь / FIFO | Нет (при наличии места) | Нет | Возвращает `false` |

---

## Общие требования ко всем примитивам

- Ровно **один producer** и **один consumer** (SPSC).
- `T` должен удовлетворять `std::is_trivially_copyable_v<T> == true`.
- Операции **не реентерабельны** (нет вложенных IRQ/NMI).
- Нет динамической памяти, нет исключений, нет системных вызовов.
- `std::atomic` с `is_always_lock_free == true` на целевой платформе.

---

## Структура каждого компонента

Все примитивы следуют единому паттерну:

```
<Name>Core<T>       — носитель разделяемого состояния (поля публичны,
                      layout и инварианты явны и проверяемы)
<Name>Writer<T>     — producer-view (только запись)
<Name>Reader<T>     — consumer-view (только чтение)
<Name><T>           — convenience wrapper (создаёт Core, выдаёт Writer/Reader)
```

---

## Тесты

```
tests/primitives/
    crc32_rt_test.cpp
    dbl_buffer_test.cpp
    mailbox2slot_test.cpp
    spsc_ring_test.cpp
```

Запуск через CTest:

```sh
ctest -L primitives
```
