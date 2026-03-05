# SPMCSnapshot (SPMC Snapshot Channel)

`docs/contracts/SPMCSnapshot — RT Contract & Invariants.md` · Revision 4.1 — February 2026

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
| Miss при NONE | нет | нет | нет |
| N reader'ов одного слота | независимы | **корректно** | независимы |
| N фиксировано | да | да | нет |

---

## Модель

### Участники

* **Writer**: ровно один поток/ядро, пишет в слоты и управляет `last_published_mask`.
* **Reader[0..N-1]**: N потоков/ядер, читают данные и управляют `busy_mask` / `refcount[i]`.

### Параметры

* `N` — максимальное число одновременных reader'ов (фиксировано на этапе компиляции)
* `K = N + 1` — число слотов

### Память

```
S[0..K-1]             — слоты данных типа T, каждый на отдельной cache line
refcount[0..K-1]      — atomic<uint8_t>, точный счётчик reader'ов читающих S[i]
busy_mask             — atomic<uint32_t>, консервативный индикатор занятости для writer'а
last_published_mask   — atomic<uint32_t>, ровно один бит: где лежит свежий снапшот
```

### Роли атомиков

| Атомик | Кто пишет | Кто читает | Назначение |
|---|---|---|---|
| `refcount[i]` | Reader'а (fetch_add / fetch_sub) | Reader'а (результат fetch_sub) | Точный подсчёт одновременных читателей |
| `busy_mask` | Reader'а (fetch_or / fetch_and) | Writer (acquire load) | O(1) консервативный индикатор занятости |
| `last_published_mask` | Writer (store) | Reader'а (acquire load) | Указатель на свежий снапшот |

### Инвариант связности refcount и busy_mask

`busy_mask` является **консервативным** индикатором занятости:

```
busy_mask[i] == 0  →  refcount[i] == 0  (строго: бит снят только после refcount→0)
busy_mask[i] == 1  →  refcount[i] >= 0  (бит может опережать счётчик в узком окне)
```

Ложноположительное значение (`busy_mask[i]==1` при `refcount[i]==0`) **допустимо** — writer консервативно не выберет слот, что безопасно. Ложноотрицательное (`busy_mask[i]==0` при `refcount[i]>0`) **недопустимо** и исключается порядком операций (I5).

Порядок операций:

* **Установка claim:** `busy_mask.fetch_or` **до** `refcount.fetch_add` — бит опережает счётчик, writer гарантированно видит занятость
* **Снятие claim:** `refcount.fetch_sub` **до** `busy_mask.fetch_and` — счётчик падает до 0 прежде чем бит снимается

### Два ортогональных состояния слота

| `busy_mask[j]` | `last_published_mask[j]` | Смысл | Writer может писать? |
|---|---|---|---|
| 0 | 0 | свободен, не свежий | ✓ идеально |
| 0 | 1 | свежий, никто не читает | ✓ (вынужденно) |
| 1 | 0 | читается, не свежий | ✗ |
| 1 | 1 | свежий и читается | ✗ |

Writer предпочитает слот где оба бита = 0 — это сохраняет свежий снапшот нетронутым.

### Атомики и барьеры

> **Memory model:** C++11 `std::atomic`. Запрещено `memory_order_relaxed` для `last_published_mask`, `busy_mask` и `refcount[i]` в операциях влияющих на корректность.

* `refcount[i]` — `atomic<uint8_t>`, достаточно при N ≤ 255
* `busy_mask` — `atomic<uint32_t>` (N ≤ 31) или `atomic<uint64_t>` (N ≤ 63)
* `last_published_mask` — тот же тип что `busy_mask`

---

## Инициализация

* `last_published_mask = 0`
* `busy_mask = 0`
* `refcount[i] = 0` для всех i
* Содержимое `S[0..K-1]` не определено до первой публикации

Reader до первой публикации всегда получает `false`.

---

## Псевдокод

```cpp
// Writer
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint32_t last = last_published_mask.load(std::memory_order_acquire);

    // Предпочесть слот который не содержит свежий снапшот.
    // Это минимизирует verify-fail у reader'ов — свежий слот не затрагивается.
    const uint32_t preferred = ~busy & ~last;
    const uint8_t  j = (preferred != 0)
        ? static_cast<uint8_t>(__builtin_ctz(preferred))   // идеально
        : static_cast<uint8_t>(__builtin_ctz(~busy));       // вынуждено

    // Записать данные и атомарно переставить указатель свежести.
    // Нет NONE, нет invalidate — last_published_mask всегда валиден.
    slots[j] = value;
    last_published_mask.store(1u << j, std::memory_order_release);
}

// Reader
bool try_read(T& out) noexcept {
    // Шаг 1: узнать где лежит свежий снапшот.
    const uint32_t last = last_published_mask.load(std::memory_order_acquire);
    if (last == 0) {
        return false;  // нет публикаций
    }
    const uint8_t p1 = static_cast<uint8_t>(__builtin_ctz(last));

    // Шаг 2: установить claim.
    // ПОРЯДОК КРИТИЧЕН:
    //   сначала busy_mask — writer видит слот занятым немедленно;
    //   затем refcount    — точный счётчик для управления снятием бита.
    // Ложноположительный busy_mask (бит есть, refcount ещё 0) безопасен.
    // Ложноотрицательный (бит снят, refcount > 0) исключён этим порядком.
    busy_mask.fetch_or(1u << p1, std::memory_order_acq_rel);
    refcount[p1].fetch_add(1, std::memory_order_acq_rel);

    // Шаг 3: verify — last_published_mask не изменился после установки claim?
    const uint32_t last2 = last_published_mask.load(std::memory_order_acquire);
    if (last2 != last) {
        // Writer опубликовал новое между шагами 1 и 3 — снять claim.
        // ПОРЯДОК КРИТИЧЕН: сначала refcount, потом busy_mask.
        if (refcount[p1].fetch_sub(1, std::memory_order_acq_rel) == 1) {
            busy_mask.fetch_and(~(1u << p1), std::memory_order_release);
        }
        return false;
    }

    // Шаг 4: безопасно копировать — слот стабилен.
    out = slots[p1];

    // Шаг 5: снять claim.
    // ПОРЯДОК КРИТИЧЕН: сначала refcount до нуля, затем снять бит.
    // Последний reader (fetch_sub вернул 1) снимает бит в busy_mask.
    if (refcount[p1].fetch_sub(1, std::memory_order_acq_rel) == 1) {
        busy_mask.fetch_and(~(1u << p1), std::memory_order_release);
    }
    return true;
}
```

---

## Инварианты (Safety)

### I1. Единоличная запись

Только writer выполняет записи в `S[i]` и изменяет `last_published_mask`.

### I2. Управление busy_mask и refcount

Только reader'а изменяют `busy_mask` и `refcount[i]`. Writer только **читает** `busy_mask` (acquire).

### I3. Запрет записи в занятый слот

Writer не начинает запись в слот `j`, если на момент принятия решения `busy_mask[j] == 1`.

Временна́я привязка: writer однократно читает `busy_mask` (acquire), вычисляет `j`, и только после этого начинает запись. Повторная проверка не требуется — защита обеспечивается verify-шагом reader'а (I5).

### I4. Чтение только под claim

Reader читает `S[i]` только при удержании claim: между `fetch_or(1<<i)` на `busy_mask` и финальным `fetch_and(~(1<<i))` на `busy_mask`.

### I5. Порядок установки и снятия claim

**Установка** (строго в этом порядке):
1. `busy_mask.fetch_or(1<<p1, acq_rel)` — writer видит слот занятым немедленно
2. `refcount[p1].fetch_add(1, acq_rel)` — фиксируем в счётчике

**Снятие** (строго в этом порядке):
1. `val = refcount[p1].fetch_sub(1, acq_rel)` — уменьшаем счётчик
2. если `val == 1`: `busy_mask.fetch_and(~(1<<p1), release)` — последний reader снимает бит

Этот порядок гарантирует:
- Ложноположительный `busy_mask` (бит есть, refcount = 0) **допустим** — окно между шагами 1 и 2 при установке
- Ложноотрицательный `busy_mask` (бит снят, refcount > 0) **невозможен** — бит снимается только после `refcount → 0`

### I6. Инвариант last_published_mask

`last_published_mask` содержит ровно один установленный бит после первой публикации, и 0 до неё. Writer всегда выполняет полный `store(1u << j)` — не RMW.

### Лемма: Safe Slot Availability

> При K = N+1 в `busy_mask` всегда существует свободный бит.
> Доказательство: N reader'ов одновременно устанавливают не более N различных битов (N reader'ов читающих один слот устанавливают один и тот же бит). Итого установлено не более N бит из K = N+1. Хотя бы один бит всегда равен 0. `ctz(~busy_mask)` возвращает его индекс за O(1). Writer находит свободный слот **всегда и за одну операцию**.

#### Замечание об ABA

Writer выбрал слот `j` (бит свободен при `load`). Reader между `load(busy_mask)` writer'а и `slots[j] = value` устанавливает `busy_mask[j]`:

1. Reader: `busy_mask.fetch_or(1<<j, acq_rel)` — **release**
2. Writer: уже выбрал `j`, начинает писать в `S[j]` — data race возможен
3. Reader: verify → `last2 != last` (writer сменит `last_published_mask`) → снимает claim **без чтения**
4. Torn read исключён: reader не читает `S[j]` после verify-fail

Happens-before: `fetch_or(acq_rel)` reader'а → `load(acquire)` writer'а в **следующем** `publish()` гарантирует что при повторном выборе слота writer увидит актуальный `busy_mask`. Текущая запись защищена verify-шагом.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

Если `try_read(out)` возвращает `true`, то `out` является консистентной копией состояния `T`, опубликованного writer'ом.

### G2. Отсутствие torn read

Невозможно пересечение «writer пишет `S[i]`» и «reader читает `S[i]`» при соблюдении I3–I5.

### G3. Корректность при N reader'ах одного слота

Несколько reader'ов могут одновременно читать один слот. `refcount[i]` корректно отслеживает их количество. `busy_mask[i]` снимается только когда **последний** reader завершает чтение.

### G4. Latest-at-claim (freshness)

При успешном `try_read` reader получает состояние опубликованное не позднее момента `load(last2)` в шаге verify, при условии `last2 == last`.

### G5. No delivery guarantee

Промежуточные публикации могут быть перезаписаны.

### G6. Bounded WCET

* Writer: 2× `load` + `ctz` + `copy(T)` + `store` — **фиксированное число операций**
* Reader: `load` + `ctz` + `fetch_or` + `fetch_add` + `load` + `copy(T)` + `fetch_sub` + (условно) `fetch_and` — **фиксированное число операций**

### G7. Progress

* Writer — **wait-free, O(1)**
* Reader — **wait-free, O(1)**

### G8. Отсутствие окна NONE

`last_published_mask` никогда не принимает значение 0 после первой публикации. Reader никогда не получает `false` из-за окна invalidate.

---

## Задержка свежести

| Компонент | Величина |
|---|---|
| Период публикации writer'а | `[0, T_w]` |
| Период опроса reader'а | `[0, T_r]` |
| Verify-fail (гонка) | `+T_r` (редко, минимизируется стратегией writer'а) |
| Аппаратная видимость | `~0..50 нс` |

**Worst-case: `T_w + 2×T_r`**

**Типичный случай: `T_w + T_r`** — окно NONE устранено, verify-fail редок благодаря предпочтению нейтрального слота.

---

## Требования к типу T

```
std::is_trivially_copyable<T>::value == true
```

---

## Обязательные compile-time требования

* `N >= 1`, `K = N + 1`
* `N <= 31` при `uint32_t`; `N <= 63` при `uint64_t`
* `N <= 254` для `refcount` типа `uint8_t`
* `std::atomic<uint8_t>::is_always_lock_free == true`
* `std::atomic<uint32_t>::is_always_lock_free == true`
* `std::is_trivially_copyable<T>::value == true`

*(Опционально)* `last_published_mask` и `busy_mask` на отдельной cache line от `refcount[]` и данных слотов.

*(Опционально)* Round-robin поиск от последнего использованного слота — равномерный износ слотов. Не влияет на корректность.

---

## Ограничения (Non-goals)

* Не очередь событий — доставка каждой публикации не гарантируется.
* N фиксировано на этапе компиляции.
* Не поддерживает более 31/63 одновременных reader'ов без смены типа масок.
