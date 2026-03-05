# SPMCSnapshot (SPMC Snapshot Channel)

`docs/contracts/SPMCSnapshot — RT Contract & Invariants.md` · Revision 5.0 — February 2026

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
| Данные при записи writer'а | нет (NONE) | **prev снапшот** | нет (retry) |
| N reader'ов одного слота | независимы | **корректно** | независимы |
| N фиксировано | да | да | нет |

---

## Модель

### Участники

* **Writer**: ровно один поток/ядро, пишет в слоты, управляет `last_published_mask` и `prev_published_mask`.
* **Reader[0..N-1]**: N потоков/ядер, читают данные и управляют `busy_mask` / `refcount[i]`.

### Параметры

* `N` — максимальное число одновременных reader'ов (фиксировано на этапе компиляции)
* `K = N + 1` — число слотов

### Память

```
S[0..K-1]             — слоты данных типа T, каждый на отдельной cache line
refcount[0..K-1]      — atomic<uint8_t>, точный счётчик reader'ов читающих S[i]
busy_mask             — atomic<uint32_t>, консервативный индикатор занятости для writer'а
last_published_mask   — atomic<uint32_t>, ровно один бит (или 0 в окне записи)
prev_published_mask   — atomic<uint32_t>, ровно один бит: предыдущая публикация
```

### Роли атомиков

| Атомик | Кто пишет | Кто читает | Назначение |
|---|---|---|---|
| `refcount[i]` | Reader'а | Reader'а (результат fetch_sub) | Точный счётчик одновременных читателей |
| `busy_mask` | Reader'а | Writer (acquire) | O(1) консервативный индикатор занятости |
| `last_published_mask` | Writer | Reader'а (acquire) | Указатель на свежий снапшот; 0 = идёт запись |
| `prev_published_mask` | Writer | Reader'а (acquire) | Fallback: предпоследний валидный снапшот |

### Семантика last_published_mask и prev_published_mask

```
last_published_mask == 1<<j  →  S[j] содержит актуальный снапшот, запись завершена
last_published_mask == 0     →  идёт запись; использовать prev_published_mask
prev_published_mask == 1<<j  →  S[j] содержит предыдущий валидный снапшот
prev_published_mask == 0     →  нет ни одной завершённой публикации
```

Reader при `last == 0` **не получает `false`** — он читает `prev_published_mask` и получает предпоследнее состояние. `false` возвращается только если обе маски равны 0 (до первой публикации).

### Инвариант связности refcount и busy_mask

`busy_mask` является **консервативным** индикатором:

```
busy_mask[i] == 0  →  refcount[i] == 0  (строго)
busy_mask[i] == 1  →  refcount[i] >= 0  (бит опережает счётчик в узком окне)
```

Порядок операций:
* **Установка claim:** `busy_mask.fetch_or` **до** `refcount.fetch_add`
* **Снятие claim:** `refcount.fetch_sub` **до** `busy_mask.fetch_and` (только при переходе 1→0)

### Два ортогональных состояния слота

| `busy_mask[j]` | `last_published_mask[j]` | Смысл | Writer может писать? |
|---|---|---|---|
| 0 | 0 | свободен, не свежий | ✓ идеально |
| 0 | 1 | свежий, никто не читает | ✓ (вынужденно) |
| 1 | 0 | читается, не свежий | ✗ |
| 1 | 1 | свежий и читается | ✗ |

Writer предпочитает слот где оба бита = 0.

### Атомики и барьеры

> **Memory model:** C++11 `std::atomic`. Запрещено `memory_order_relaxed` для всех управляющих атомиков в операциях влияющих на корректность.

* `refcount[i]` — `atomic<uint8_t>`, N ≤ 255
* `busy_mask`, `last_published_mask`, `prev_published_mask` — `atomic<uint32_t>` (N ≤ 31) или `atomic<uint64_t>` (N ≤ 63)

---

## Инициализация

* `last_published_mask = 0`
* `prev_published_mask = 0`
* `busy_mask = 0`
* `refcount[i] = 0` для всех i
* Содержимое `S[0..K-1]` не определено до первой публикации

Reader до первой завершённой публикации всегда получает `false`.

---

## Псевдокод

```cpp
// Writer
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint32_t last = last_published_mask.load(std::memory_order_acquire);

    // Предпочесть слот который не содержит свежий снапшот.
    const uint32_t preferred = ~busy & ~last;
    const uint8_t  j = (preferred != 0)
        ? static_cast<uint8_t>(__builtin_ctz(preferred))
        : static_cast<uint8_t>(__builtin_ctz(~busy));

    // Сохранить текущую публикацию как предыдущую.
    // prev всегда указывает на последний завершённый снапшот.
    if (last != 0) {
        prev_published_mask.store(last, std::memory_order_release);
    }

    // Снять флаг публикации — сигнал reader'ам: данные нестабильны.
    // Reader'а переключатся на prev_published_mask.
    last_published_mask.store(0, std::memory_order_release);

    // Записать данные.
    slots[j] = value;

    // Выставить флаг публикации — данные готовы.
    last_published_mask.store(1u << j, std::memory_order_release);
}

// Reader
bool try_read(T& out) noexcept {
    // Шаг 1: определить источник снапшота.
    const uint32_t last = last_published_mask.load(std::memory_order_acquire);
    const bool     using_prev = (last == 0);

    uint32_t ref;
    if (!using_prev) {
        ref = last;                    // свежий снапшот доступен
    } else {
        ref = prev_published_mask.load(std::memory_order_acquire);
        if (ref == 0) return false;    // нет ни одной завершённой публикации
    }

    const uint8_t p1 = static_cast<uint8_t>(__builtin_ctz(ref));

    // Шаг 2: установить claim.
    // ПОРЯДОК КРИТИЧЕН: busy_mask до refcount.
    busy_mask.fetch_or(1u << p1, std::memory_order_acq_rel);
    refcount[p1].fetch_add(1, std::memory_order_acq_rel);

    // Шаг 3: verify — источник снапшота не изменился?
    const uint32_t check = using_prev
        ? prev_published_mask.load(std::memory_order_acquire)
        : last_published_mask.load(std::memory_order_acquire);

    if (check != ref) {
        // Источник изменился — снять claim.
        // ПОРЯДОК КРИТИЧЕН: refcount до busy_mask.
        if (refcount[p1].fetch_sub(1, std::memory_order_acq_rel) == 1) {
            busy_mask.fetch_and(~(1u << p1), std::memory_order_release);
        }
        return false;
    }

    // Шаг 4: безопасно копировать — слот стабилен.
    out = slots[p1];

    // Шаг 5: снять claim.
    if (refcount[p1].fetch_sub(1, std::memory_order_acq_rel) == 1) {
        busy_mask.fetch_and(~(1u << p1), std::memory_order_release);
    }
    return true;
}
```

---

## Инварианты (Safety)

### I1. Единоличная запись

Только writer выполняет записи в `S[i]`, изменяет `last_published_mask` и `prev_published_mask`.

### I2. Управление busy_mask и refcount

Только reader'а изменяют `busy_mask` и `refcount[i]`. Writer только **читает** `busy_mask` (acquire).

### I3. Запрет записи в занятый слот

Writer не начинает запись в слот `j`, если на момент принятия решения `busy_mask[j] == 1`.

Временна́я привязка: writer однократно читает `busy_mask` (acquire), вычисляет `j`, снимает `last_published_mask`, и только после этого начинает запись. Повторная проверка не требуется — защита обеспечивается verify-шагом reader'а (I5).

### I4. Чтение только под claim

Reader читает `S[i]` только при удержании claim: `refcount[i] > 0` на протяжении всего чтения (шаги 2–5 псевдокода).

### I5. Порядок установки и снятия claim

**Установка** (строго в этом порядке):
1. `busy_mask.fetch_or(1<<p1, acq_rel)` — writer видит занятость немедленно
2. `refcount[p1].fetch_add(1, acq_rel)` — фиксируем счётчик

**Снятие** (строго в этом порядке):
1. `val = refcount[p1].fetch_sub(1, acq_rel)` — уменьшаем счётчик
2. если `val == 1`: `busy_mask.fetch_and(~(1<<p1), release)` — последний reader снимает бит

### I6. Инвариант prev_published_mask

`prev_published_mask` содержит ровно один установленный бит после первой публикации, и 0 до неё. Обновляется writer'ом **до** снятия `last_published_mask` — в любой момент когда `last == 0`, `prev` указывает на последний завершённый снапшот.

### I7. Инвариант last_published_mask

`last_published_mask` содержит ровно один установленный бит когда запись не идёт, и 0 в окне записи (`store(0)` → `copy(T)` → `store(1<<j)`). Writer всегда выполняет полный `store` — не RMW.

### I8. Корректность verify при использовании prev

Если reader использует `prev_published_mask` как источник (`using_prev == true`), verify проверяет именно `prev_published_mask`. Это корректно: writer обновляет `prev` только **до** снятия `last`. Пока `last == 0`, `prev` стабилен.

Исключение: writer завершил запись и выставил новый `last` между шагами 1 и 3 reader'а. Тогда:
- `last` стал ненулевым → `prev` мог обновиться на следующей публикации
- `check = prev_published_mask.load()` вернёт новое значение
- `check != ref` → verify-fail → claim снят без чтения ✓

### Лемма: Safe Slot Availability

> При K = N+1 в `busy_mask` всегда существует свободный бит.
> Доказательство: N reader'ов одновременно устанавливают не более N различных битов. Итого установлено не более N бит из K = N+1. Хотя бы один бит всегда равен 0.

#### Замечание об ABA

Пока reader держит claim на слот `j` (`busy_mask[j]==1`, `refcount[j]>0`) — writer не может писать в `S[j]` по I3. Следовательно данные в `S[j]` стабильны на всё время чтения. ABA через `last_published_mask` или `prev_published_mask` структурно невозможен при удержании claim.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

Если `try_read(out)` возвращает `true`, то `out` является консистентной копией состояния `T`, опубликованного writer'ом (текущего или предыдущего).

### G2. Отсутствие torn read

Невозможно пересечение «writer пишет `S[i]`» и «reader читает `S[i]`» при соблюдении I3–I5.

### G3. Корректность при N reader'ах одного слота

`refcount[i]` корректно отслеживает количество одновременных читателей. `busy_mask[i]` снимается только когда последний reader завершает чтение.

### G4. Свежесть при занятых слотах

Если все слоты кроме `last_published_mask` слота заняты, writer вынужден писать в него. Reader'а в окне записи (`last==0`) читают `prev_published_mask` — предпоследний валидный снапшот. Задержка свежести: одна публикация назад вместо целого тика.

### G5. Latest-at-claim (freshness)

При успешном `try_read` reader получает состояние опубликованное не позднее момента `load(check)` в шаге verify, при условии `check == ref`.

### G6. No delivery guarantee

Промежуточные публикации могут быть перезаписаны.

### G7. Bounded WCET

* Writer: 2× `load` + `ctz` + (условно) `store(prev)` + `store(0)` + `copy(T)` + `store(1<<j)` — **фиксированное число операций**
* Reader: `load` + (условно) `load(prev)` + `ctz` + `fetch_or` + `fetch_add` + `load` + `copy(T)` + `fetch_sub` + (условно) `fetch_and` — **фиксированное число операций**

### G8. Progress

* Writer — **wait-free, O(1)**
* Reader — **wait-free, O(1)**

### G9. Минимизация ложных промахов

Reader возвращает `false` только при отсутствии любых данных (до первой публикации). В окне записи writer'а reader использует `prev_published_mask` — данные всегда доступны после первой публикации.

---

## Задержка свежести

| Сценарий | Задержка |
|---|---|
| Свежий снапшот доступен | `[0, T_w] + [0, T_r]` |
| Окно записи, читается prev | `[T_w, 2×T_w] + [0, T_r]` |
| Verify-fail | `+T_r` |

**Worst-case: `2×T_w + 2×T_r`** — окно записи + verify-fail

**Типичный случай: `T_w + T_r`** — свежий снапшот доступен сразу

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

*(Опционально)* `last_published_mask`, `prev_published_mask` и `busy_mask` на отдельной cache line от `refcount[]` и данных слотов.

*(Опционально)* Round-robin поиск от последнего использованного слота — равномерный износ.

---

## Ограничения (Non-goals)

* Не очередь событий — доставка каждой публикации не гарантируется.
* N фиксировано на этапе компиляции.
* Не поддерживает более 31/63 одновременных reader'ов без смены типа масок.
