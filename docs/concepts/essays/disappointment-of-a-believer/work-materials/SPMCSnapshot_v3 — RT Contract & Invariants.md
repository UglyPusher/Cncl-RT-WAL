# SPMCSnapshot (SPMC Snapshot Channel)

`docs/contracts/SPMCSnapshot — RT Contract & Invariants.md` · Revision 3.0 — February 2026

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
| Miss при NONE | нет | **нет** | нет |
| N фиксировано | да | да | нет |

---

## Модель

### Участники

* **Writer**: ровно один поток/ядро, только он пишет в слоты и управляет `last_published_mask`.
* **Reader[0..N-1]**: N потоков/ядер, каждый только читает данные и управляет `busy_mask`.

### Параметры

* `N` — максимальное число одновременных reader'ов (фиксировано на этапе компиляции)
* `K = N + 1` — число слотов (минимально необходимое для wait-free writer'а)

### Память

* `K` слотов: `S[0..K-1]` типа `T`, каждый на отдельной cache line
* `last_published_mask` — битовая маска с **ровно одним установленным битом**: указывает reader'ам где лежит самый свежий снапшот
* `busy_mask` — битовая маска занятости: бит `i` установлен ↔ слот `i` активно читается прямо сейчас

### Ключевое архитектурное решение

`pub_state` и состояние `NONE` **отсутствуют**. Два атомарных `uint32_t` полностью описывают состояние системы:

```
last_published_mask  — ровно 1 бит: "здесь самый свежий снапшот"
busy_mask            — 0..N битов:  "эти слоты сейчас читаются"
```

Reader всегда знает куда смотреть. Окно `NONE` (invalidate) как источник промахов **устранено**.

### Два ортогональных состояния слота

| `busy_mask[j]` | `last_published_mask[j]` | Смысл | Writer может писать? |
|---|---|---|---|
| 0 | 0 | свободен, не свежий | ✓ идеально |
| 0 | 1 | свежий, никто не читает | ✓ (вынужденно, при отсутствии лучшего) |
| 1 | 0 | читается, не свежий | ✗ |
| 1 | 1 | свежий и читается | ✗ |

Writer **предпочитает** слот где оба бита = 0. Это сохраняет свежий снапшот нетронутым пока reader'а его читают.

### Атомики и барьеры

> **Memory model:** Реализация обязана использовать `std::atomic<T>` из C++11 (или новее) с операциями `load(acquire)`, `store(release)`, `fetch_or(acq_rel)`, `fetch_and(release)`. Использование `memory_order_relaxed` запрещено для `last_published_mask` и `busy_mask` в операциях влияющих на корректность протокола.

* `last_published_mask` — `atomic<uint32_t>`, lock-free, ровно один бит установлен после первой публикации
* `busy_mask` — `atomic<uint32_t>`, lock-free, 0..N битов установлено

---

## Инициализация

После создания объекта:

* `last_published_mask = 0` *(нет публикаций)*
* `busy_mask = 0`
* Содержимое слотов `S[0..K-1]` не определено до первой публикации

Reader, вызывающий `try_read` до первой публикации, получает `false` (по `last_published_mask == 0`).

---

## API семантика

### Writer

`publish(value)` — публикует новое состояние. Алгоритм выбора слота:

1. Загрузить `busy_mask` и `last_published_mask` (acquire)
2. Вычислить `free = ~busy_mask & ~last_published_mask` — слоты свободные И не свежие
3. Если `free != 0`: `j = ctz(free)` — **идеальный выбор**, свежий снапшот не затрагивается
4. Иначе: `j = ctz(~busy_mask)` — все остальные слоты заняты reader'ами, вынуждены перезаписать свежий
5. Записать данные в `S[j]`
6. `last_published_mask.store(1u << j, release)` — атомарно переставить указатель свежести

По Лемме `~busy_mask` всегда ненулевой при K = N+1 — шаги 3 или 4 **всегда** находят слот.

**Нет invalidate, нет NONE** — `last_published_mask` переставляется атомарно, читатели видят либо старый либо новый адрес, оба валидны.

### Reader

`try_read(out)`:

* Делает bounded attempt получить консистентный снапшот
* Если `last_published_mask == 0` → нет публикаций → `return false`
* При гонке публикации → verify-fail → `return false`; reader использует предыдущее состояние
* **Retry отсутствует** — при отказе reader уходит на следующий тик

#### Постусловие `try_read`

> По завершении `try_read` (независимо от возвращаемого значения) бит `p1` в `busy_mask` возвращён в исходное состояние: если claim был установлен — он снят.

---

## Псевдокод

```cpp
// Writer
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint32_t last = last_published_mask.load(std::memory_order_acquire);

    // Шаг 1: предпочесть слот который не содержит свежий снапшот.
    // Это минимизирует verify-fail у reader'ов — свежий слот не инвалидируется.
    const uint32_t preferred = ~busy & ~last;
    const uint8_t  j = (preferred != 0)
        ? static_cast<uint8_t>(__builtin_ctz(preferred))   // идеально
        : static_cast<uint8_t>(__builtin_ctz(~busy));       // вынуждено

    // Шаг 2: записать данные.
    slots[j] = value;

    // Шаг 3: атомарно переставить указатель свежести.
    // Нет NONE, нет invalidate — last_published_mask всегда валиден.
    last_published_mask.store(1u << j, std::memory_order_release);
}

// Reader
bool try_read(T& out) noexcept {
    // Шаг 1: узнать где лежит свежий снапшот.
    const uint32_t last = last_published_mask.load(std::memory_order_acquire);
    if (last == 0) {
        // Нет ни одной публикации.
        return false;
    }
    const uint8_t p1 = static_cast<uint8_t>(__builtin_ctz(last));

    // Шаг 2: установить claim — атомарно пометить слот занятым.
    // fetch_or(acq_rel): release виден writer'у через load(acquire) в publish().
    busy_mask.fetch_or(1u << p1, std::memory_order_acq_rel);

    // Шаг 3: verify — last_published_mask не изменился после установки claim?
    const uint32_t last2 = last_published_mask.load(std::memory_order_acquire);
    if (last2 != last) {
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

Только writer выполняет записи в `S[i]` и изменяет `last_published_mask`.

### I2. Управление busy_mask

Только reader'а изменяют `busy_mask` через `fetch_or` / `fetch_and`. Writer только **читает** `busy_mask` (acquire) для выбора слота.

### I3. Запрет записи в занятый слот

Writer не начинает запись в слот `j`, если на момент принятия решения `busy_mask[j] == 1`.

Временна́я привязка: writer однократно читает `busy_mask` (acquire), вычисляет `j`, и только после этого начинает запись. Повторная проверка не требуется — защита обеспечивается verify-шагом reader'а (I5).

### I4. Чтение только под claim

Reader читает `S[i]` только при удержании claim: между `fetch_or(1<<i)` и `fetch_and(~(1<<i))` для `busy_mask`.

### I5. Claim-verify у reader

Reader обязан подтверждать что `last_published_mask` не изменился после установки claim:

1. `last = last_published_mask.load(acquire)`
2. если `last == 0` → `return false` *(нет публикаций)*
3. `p1 = ctz(last)`
4. `busy_mask.fetch_or(1<<p1, acq_rel)` ← **release**: visible to writer's acquire-load
5. `last2 = last_published_mask.load(acquire)`
6. если `last2 != last` → `busy_mask.fetch_and(~(1<<p1), release)`, `return false`

Только если `last2 == last` reader имеет право читать `S[p1]`.

### I6. Инвариант last_published_mask

`last_published_mask` содержит **ровно один установленный бит** после первой публикации, и **0** до неё. Это гарантируется тем что writer всегда выполняет `store(1u << j)` — не RMW, а полную замену значения.

### Лемма: Safe Slot Availability

> При K = N+1 в `~busy_mask` всегда существует свободный бит.
> Доказательство: N reader'ов могут одновременно удерживать claim — каждый устанавливает ровно один бит (по I4). Следовательно установлено не более N бит из K = N+1. Хотя бы один бит `busy_mask` всегда равен 0. `ctz(~busy_mask)` возвращает его индекс за O(1). Writer находит свободный слот **всегда и за одну операцию**.

#### Замечание об ABA

Если writer выбрал слот `j` (бит свободен в момент `load`), а reader между этим `load` и `last_published_mask.store` успел установить `busy_mask[j]`:

1. Reader выполнил `fetch_or(1<<j, acq_rel)` — **release**
2. Writer выполнил `load(busy_mask, acquire)` **до** этого — не видел бит
3. Writer записал данные в `S[j]` и выполнил `last_published_mask.store(1<<j)`
4. Reader на шаге verify: `last2 != last` (last_published_mask изменился) → снимает claim без чтения
5. Torn read исключён: reader не читает `S[j]` после verify-fail

Happens-before между `fetch_or(acq_rel)` reader'а и `load(acquire)` writer'а в **следующем** вызове `publish()` гарантирует что при повторном выборе слота writer увидит актуальный `busy_mask`.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

Если `try_read(out)` возвращает `true`, то `out` является консистентной копией состояния `T`, опубликованного writer'ом.

### G2. Отсутствие torn read

Невозможно пересечение «writer пишет `S[i]`» и «reader читает `S[i]`» при соблюдении I3–I5.

### G3. Latest-at-claim (freshness)

При успешном `try_read` reader получает состояние опубликованное не позднее момента `load(last2)` в шаге verify (шаг 5 в I5), при условии `last2 == last`.

### G4. No delivery guarantee

Не гарантируется что каждый reader увидит каждую публикацию. Промежуточные публикации могут быть перезаписаны.

### G5. Bounded WCET

* Writer: `load(busy_mask)` + `load(last_published_mask)` + `ctz` + `copy(T)` + `store(last_published_mask)` — **фиксированное число операций, нет ветвлений влияющих на количество атомарных операций**
* Reader: `load` + `ctz` + `fetch_or` + `load` + `copy(T)` + `fetch_and` — **фиксированное число операций**

### G6. Progress

* Writer — **wait-free, O(1)**: `ctz(~busy_mask)` детерминированно находит свободный слот. Нет итераций, нет зависимости от действий reader'ов.
* Reader — **wait-free, O(1)**: фиксированная последовательность операций, retry отсутствует.

### G7. Отсутствие окна NONE

`last_published_mask` никогда не принимает состояние "нет публикаций" после первой публикации. Reader никогда не получает `false` из-за окна invalidate — только из-за отсутствия данных (до первой публикации) или verify-fail (гонка публикации).

---

## Задержка свежести

Задержка между публикацией writer'а и получением reader'ом складывается из:

| Компонент | Величина |
|---|---|
| Период публикации writer'а | `[0, T_w]` |
| Период опроса reader'а | `[0, T_r]` |
| Verify-fail (гонка) | `+T_r` (редко) |
| Аппаратная видимость | `~0..50 нс` |

**Worst-case: `T_w + 2×T_r`** (пропустил публикацию + verify-fail)

**Типичный случай: `T_w + T_r`** — окно NONE устранено, verify-fail редок благодаря предпочтению "нейтрального" слота при выборе.

Для сравнения: `Mailbox2Slot` (SPSC) имеет тот же worst-case `T_w + 2×T_r` при одном reader'е. `SPMCSnapshot` достигает этого же результата для N reader'ов.

---

## Требования к типу T

```
std::is_trivially_copyable<T>::value == true
```

---

## Обязательные compile-time требования

* `N >= 1`
* `K = N + 1`
* `N <= 31` при `uint32_t`; `N <= 63` при `uint64_t`
* `std::atomic<uint32_t>::is_always_lock_free == true`
* `std::is_trivially_copyable<T>::value == true`

*(Опционально)* `last_published_mask` и `busy_mask` на отдельной cache line от данных слотов.

*(Опционально)* Round-robin поиск от последнего использованного слота — равномерный износ. Не влияет на корректность.

---

## Ограничения (Non-goals)

* Не очередь событий — каждая публикация не гарантирует доставку всем reader'ам.
* N фиксировано на этапе компиляции.
* Не поддерживает более 31 (или 63) одновременных reader'ов без смены типа масок.
