# SPMCSnapshot (SPMC Snapshot Channel)

`docs/contracts/SPMCSnapshot — RT Contract & Invariants.md` · Revision 2.0 — February 2026

---

## Назначение

Примитив передачи **снапшота состояния** от одного писателя к нескольким читателям.

**Семантика: latest-wins.** Промежуточные публикации могут быть потеряны.

Оптимизирован для систем с ограниченной памятью: требует `(N+1) × sizeof(T)` памяти на данные против `2N × sizeof(T)` для N независимых Mailbox2Slot. Writer и все reader'а — **wait-free, O(1)**.

---

## Сравнение с альтернативами

| | N × Mailbox2Slot | SPMCSnapshot | SeqLock |
|---|---|---|---|
| Память (данные) | `2N × sizeof(T)` | `(N+1) × sizeof(T)` | `1 × sizeof(T)` |
| Writer | wait-free, O(N) записей | **wait-free, O(1)** | wait-free, O(1) |
| Reader | wait-free, O(1) | wait-free, O(1) | не wait-free (bounded retry) |
| N фиксировано | да | да | нет |

---

## Модель

### Участники

* **Writer**: ровно один поток/ядро, только он пишет в слоты и управляет `pub_state`.
* **Reader[0..N-1]**: N потоков/ядер, каждый только читает данные и управляет `busy_mask`.

### Параметры

* `N` — максимальное число одновременных reader'ов (фиксировано на этапе компиляции)
* `K = N + 1` — число слотов (минимально необходимое для wait-free writer'а)

### Память

* `K` слотов: `S[0..K-1]` типа `T`, каждый на отдельной cache line
* `pub_state ∈ {0..K-1, NONE}` — индекс последнего опубликованного слота
* `busy_mask` — битовая маска занятости: бит `i` установлен ↔ слот `i` активно читается

### Два ортогональных состояния слота

`busy_mask` и `pub_state` описывают **независимые** аспекты:

```
busy_mask бит j = 1   →   слот j читается прямо сейчас
pub_state == j        →   слот j содержит последнюю публикацию
```

Их комбинации:

| `busy_mask[j]` | `pub_state == j` | Смысл | Writer может писать? |
|---|---|---|---|
| 0 | 0 | свободен, не опубликован | ✓ сразу |
| 0 | 1 | опубликован, никто не читает | ✓ invalidate → писать |
| 1 | 0 | читается, перекрыт новой публикацией | ✗ |
| 1 | 1 | опубликован и читается | ✗ |

Writer выбирает любой слот где `busy_mask[j] == 0`.

### Атомики и барьеры

> **Memory model:** Реализация обязана использовать `std::atomic<T>` из C++11 (или новее) с операциями `load(acquire)`, `store(release)`, `fetch_or(acq_rel)`, `fetch_and(release)`. Использование `memory_order_relaxed` запрещено для `pub_state` и `busy_mask` в операциях влияющих на корректность протокола.

* `pub_state` — `atomic<uint8_t>`, lock-free
* `busy_mask` — `atomic<uint32_t>` (N ≤ 31) или `atomic<uint64_t>` (N ≤ 63), lock-free

---

## Инициализация

После создания объекта:

* `pub_state = NONE`
* `busy_mask = 0`
* Содержимое слотов `S[0..K-1]` не определено до первой публикации

Reader, вызывающий `try_read` до первой публикации, всегда получает `false`.

---

## API семантика

### Writer

`publish(value)` — публикует новое состояние. Алгоритм:

1. Загрузить `busy_mask` (acquire) — **одна атомарная операция**
2. `j = ctz(~busy_mask)` — первый свободный слот, **одна инструкция**
3. Если `pub_state == j` — invalidate: `pub_state.CAS(j → NONE, release)`
4. Записать данные в `S[j]`
5. `pub_state.store(j, release)`

По Лемме шаг 2 **всегда** находит валидный слот при K = N+1.

### Reader

`try_read(out)`:

* Делает bounded attempt получить консистентный снапшот
* Если не получилось (NONE или гонка) → возвращает `false`; reader использует предыдущее состояние
* **Retry отсутствует** — при отказе reader уходит на следующий тик

#### Постусловие `try_read`

> По завершении `try_read` (независимо от возвращаемого значения) бит `p1` в `busy_mask` возвращён в исходное состояние: если claim был установлен — он снят.

---

## Псевдокод

```cpp
// Writer
void publish(const T& value) noexcept {
    // Шаг 1-2: O(1) поиск свободного слота через bitmask.
    // По Лемме ~busy_mask всегда имеет хотя бы один установленный бит.
    const uint32_t mask = busy_mask.load(std::memory_order_acquire);
    const uint8_t  j    = static_cast<uint8_t>(__builtin_ctz(~mask));

    // Шаг 3: invalidate если слот j был опубликован.
    // CAS: если pub_state == j → NONE; иначе no-op.
    // Выполняется ровно один раз независимо от результата.
    uint8_t expected = j;
    pub_state.compare_exchange_strong(
        expected, kNone,
        std::memory_order_release,
        std::memory_order_relaxed);

    // Шаг 4-5: записать данные и опубликовать.
    slots[j] = value;
    pub_state.store(j, std::memory_order_release);
}

// Reader
bool try_read(T& out) noexcept {
    // Шаг 1: загрузить опубликованный слот.
    const uint8_t p1 = pub_state.load(std::memory_order_acquire);
    if (p1 == kNone) {
        // busy_mask не трогаем — claim не устанавливался.
        return false;
    }

    // Шаг 2: установить claim — атомарно пометить слот занятым.
    // fetch_or(acq_rel): release виден writer'у через load(acquire) в publish().
    busy_mask.fetch_or(1u << p1, std::memory_order_acq_rel);

    // Шаг 3: verify — публикация не изменилась после установки claim?
    const uint8_t p2 = pub_state.load(std::memory_order_acquire);
    if (p2 != p1) {
        // Writer опубликовал новое между шагами 1 и 3 — снять claim.
        busy_mask.fetch_and(~(1u << p1), std::memory_order_release);
        return false;
    }

    // Шаг 4: безопасно копировать — слот стабилен.
    out = slots[p1];

    // Шаг 5: снять claim.
    busy_mask.fetch_and(~(1u << p1), std::memory_order_release);
    return true;
}
```

---

## Инварианты (Safety)

### I1. Единоличная запись

Только writer выполняет записи в `S[i]` и изменяет `pub_state`.

### I2. Управление busy_mask

Только reader'а изменяют `busy_mask` через `fetch_or` / `fetch_and`. Writer только **читает** `busy_mask` (acquire) для выбора слота.

### I3. Запрет записи в занятый слот

Writer не начинает запись в слот `j`, если на момент принятия решения `busy_mask[j] == 1`.

Временна́я привязка: writer однократно читает `busy_mask` (acquire), вычисляет `j = ctz(~mask)`, и только после этого начинает запись. Повторная проверка не требуется — защита обеспечивается verify-шагом reader'а (I5).

### I4. Чтение только под claim

Reader читает `S[i]` только при удержании claim: между `fetch_or(1<<i)` и `fetch_and(~(1<<i))` для `busy_mask`.

### I5. Claim-verify у reader

Reader обязан подтверждать что публикация не изменилась после установки claim:

1. `p1 = pub_state.load(acquire)`
2. если `p1 == NONE` → `return false` *(claim не устанавливался)*
3. `busy_mask.fetch_or(1<<p1, acq_rel)` ← **release**: visible to writer's acquire-load
4. `p2 = pub_state.load(acquire)`
5. если `p2 != p1` → `busy_mask.fetch_and(~(1<<p1), release)`, `return false`

Только если `p1 == p2` reader имеет право читать `S[p1]`.

### Лемма: Safe Slot Availability

> При K = N+1 в `busy_mask` всегда существует свободный бит.
> Доказательство: N reader'ов могут одновременно удерживать claim — каждый устанавливает ровно один бит (по I4). Следовательно установлено не более N бит из K = N+1. Хотя бы один бит всегда равен 0. `ctz(~mask)` возвращает его индекс за O(1). Writer находит свободный слот **всегда и за одну операцию**.

#### Замечание об ABA

Если writer выбрал слот `j` (бит свободен в момент `load`), а reader между этим `load` и началом записи успел установить `busy_mask[j]`:

1. Reader выполнил `fetch_or(1<<j, acq_rel)` — **release**
2. Writer выполнил `load(busy_mask, acquire)` **до** этого — не видел бит
3. Writer начинает писать в `S[j]`
4. Reader на шаге verify видит `p2 != p1` (writer сменил `pub_state`) → снимает claim без чтения
5. Torn read исключён: reader не читает `S[j]` после verify-fail

Happens-before между `fetch_or(acq_rel)` reader'а и `load(acquire)` writer'а в **следующем** вызове `publish()` гарантирует что при повторном выборе слота writer увидит актуальный `busy_mask`. Текущая запись защищена verify-шагом reader'а.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

Если `try_read(out)` возвращает `true`, то `out` является консистентной копией состояния `T`, опубликованного writer'ом.

### G2. Отсутствие torn read

Невозможно пересечение «writer пишет `S[i]`» и «reader читает `S[i]`» при соблюдении I3–I5.

### G3. Latest-at-claim (freshness)

При успешном `try_read` reader получает состояние опубликованное не позднее момента `load(p2)` в шаге verify (шаг 4 в I5), при условии `p1 == p2`.

### G4. No delivery guarantee

Не гарантируется что каждый reader увидит каждую публикацию. Промежуточные публикации могут быть перезаписаны.

### G5. Bounded WCET

* Writer: `load(busy_mask)` + `ctz` + `CAS` + `copy(T)` + `store(pub_state)` — **фиксированное число операций**
* Reader: `load` + `fetch_or` + `load` + `copy(T)` + `fetch_and` — **фиксированное число операций**

### G6. Progress

* Writer — **wait-free, O(1)**: `ctz(~busy_mask)` детерминированно находит свободный слот за одну инструкцию. Нет итераций, нет зависимости от действий reader'ов.
* Reader — **wait-free, O(1)**: фиксированная последовательность операций, retry отсутствует.

---

## Требования к типу T

```
std::is_trivially_copyable<T>::value == true
```

---

## Обязательные compile-time требования

* `N >= 1`
* `K = N + 1`
* `N <= 31` при `busy_mask = uint32_t`; `N <= 63` при `busy_mask = uint64_t`
* `std::atomic<uint32_t>::is_always_lock_free == true`
* `std::atomic<uint8_t>::is_always_lock_free == true`
* `std::is_trivially_copyable<T>::value == true`

*(Опционально)* `pub_state` и `busy_mask` на отдельной cache line от данных слотов — снижает false sharing при одновременном чтении N reader'ов.

*(Опционально)* Round-robin поиск от последнего использованного слота вместо `ctz` — равномерный износ слотов. Не влияет на корректность.

---

## Ограничения (Non-goals)

* Не очередь событий — каждая публикация не гарантирует доставку всем reader'ам.
* N фиксировано на этапе компиляции — динамическое добавление reader'ов не поддерживается без пересчёта K и расширения `busy_mask`.
* Не поддерживает более 31 (или 63) одновременных reader'ов без смены типа `busy_mask`.
