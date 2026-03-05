# SPMCSnapshot (SPMC Snapshot Channel)

`docs/contracts/SPMCSnapshot — RT Contract & Invariants.md` · Revision 6.2 — February 2026

---

## Назначение

Примитив передачи **снапшота состояния** от одного писателя к нескольким читателям.

**Семантика: latest-wins.** Промежуточные публикации могут быть потеряны.

### Класс решаемых задач

Примитив решает задачу **безопасной публикации разделяемого состояния** в условиях:

* **Ограниченная память** — `(N+2) × sizeof(T)` на данные против `2N × sizeof(T)` для N независимых Mailbox2Slot. При больших `sizeof(T)` и большом N экономия существенна.
* **SMP-системы** — корректность на многопроцессорных системах с независимыми кэшами (x86/TSO, ARM, POWER). Torn write исключён через happens-before цепочку `store(release)` writer'а → `load(acquire)` reader'а на атомике `published`: все байты `slots[j]` гарантированно видимы reader'у **до** того как `published == j` становится наблюдаемым.
* **Вытеснение** — корректность сохраняется при вытеснении writer'а или reader'а планировщиком в любой точке алгоритма. Torn write при вытеснении writer'а в середине `slots[j] = value` исключён тем же барьером: `published.store(j, release)` выполняется только после возобновления writer'а и завершения записи.
* **RT-домен** — wait-free, O(1), bounded WCET для writer'а и всех reader'ов. Нет блокировок, нет динамической памяти, нет системных вызовов.

Torn read исключён **структурно** (инвариант W-NoOverwritePublished) — без verify, без retry, без SeqLock-подобных счётчиков.

---

## Сравнение с альтернативами

| | N × Mailbox2Slot | SPMCSnapshot | SeqLock |
|---|---|---|---|
| Память (данные) | `2N × sizeof(T)` | `(N+2) × sizeof(T)` | `1 × sizeof(T)` |
| Writer | wait-free, O(N) записей | **wait-free, O(1)** | wait-free, O(1) |
| Reader | wait-free, O(1) | **wait-free, O(1), без verify** | не wait-free |
| Torn read | нет | **нет (структурно)** | нет |
| Состояние NONE | нет | **нет** | нет |
| "Нет данных" до первой публикации | нет | **явный atomic<bool>** | нет |
| N фиксировано | да | да | нет |

---

## Модель

### Участники

* **Writer**: ровно один поток/ядро, пишет в слоты, управляет `published` и `initialized`.
* **Reader[0..N-1]**: N потоков/ядер, читают данные и управляют `refcnt[i]` / `busy_mask`.

### Параметры

* `N` — максимальное число одновременных reader'ов (фиксировано на этапе компиляции)
* `K = N + 2` — число слотов

### Память

```
slots[0..K-1]    — слоты данных типа T, каждый на отдельной cache line
refcnt[0..K-1]   — atomic<uint8_t>, точный счётчик reader'ов читающих slots[i]
busy_mask        — atomic<uint32_t>, консервативный индикатор занятости для writer'а
published        — atomic<uint8_t>, индекс текущего опубликованного слота ∈ [0..K-1]
initialized      — atomic<bool>,   false до первой публикации, true после
```

### Роли атомиков

| Атомик | Кто пишет | Кто читает | Назначение |
|---|---|---|---|
| `refcnt[i]` | Reader'а | Reader'а (результат fetch_sub) | Точный счётчик одновременных читателей |
| `busy_mask` | Reader'а | Writer (acquire) | O(1) консервативный индикатор занятости |
| `published` | Writer (store) | Reader'а, Writer (acquire) | Индекс актуального снапшота |
| `initialized` | Writer (store, однократно) | Reader'а (acquire) | Сигнал наличия первой публикации |

### Семантика initialized

`initialized` устанавливается в `true` writer'ом **после** первой публикации и остаётся `true` навсегда. Reader проверяет его в начале `try_read` — единственная точка возврата `false` в установившемся режиме.

После установки в `true`: `load(acquire)` на x86 — обычный `mov`, стоимость минимальна. Writer при каждой публикации выполняет `store(true, release)` — идемпотентно, `true → true` не создаёт гонок.

### Инвариант связности refcnt и busy_mask

```
busy_mask[i] == 0  →  refcnt[i] == 0  (строго)
busy_mask[i] == 1  →  refcnt[i] >= 0  (консервативно: бит опережает счётчик)
```

Порядок операций reader'а:
* **Установка claim:** `busy_mask.fetch_or(1<<i)` **до** `refcnt[i].fetch_add(1)`
* **Снятие claim:** `refcnt[i].fetch_sub(1)` **до** `busy_mask.fetch_and(~(1<<i))` (только при переходе 1→0)

---

## Теорема: Writer Slot Availability при K = N+2

> В любой момент существует слот `j` такой что:
> * `busy_mask[j] == 0` (не занят читателями)
> * `j != published`   (не опубликован)
>
> то есть writer **всегда** может выбрать свободный, незаблокированный,
> неопубликованный слот для записи.

**Доказательство:**
Reader'а могут занять максимум `N` слотов (каждый держит строго ≤ 1 слота в любой момент) ⇒ свободно минимум `K − N = 2` слота ⇒ максимум один из них может быть `published` ⇒ остаётся минимум **один** свободный не-published слот. ∎

**Следствие:** writer не трогает `published` слот **никогда**. Слот стабилен для всех reader'ов на всё время между `published.store` и следующей публикацией. Verify не нужен.

---

## Инвариант W-NoOverwritePublished

> Writer никогда не пишет в слот `j` где `j == published`.

Из этого инварианта напрямую следует:
* reader не нуждается в verify — слот не может быть перезаписан пока он опубликован
* `published` всегда валиден после первой публикации
* torn read исключён структурно, а не протокольно

---

## Инициализация объекта

* `published = 0`
* `initialized = false`
* `busy_mask = 0`
* `refcnt[i] = 0` для всех i
* Содержимое `slots[0..K-1]` не определено до первой публикации

---

## Псевдокод

```cpp
// Writer
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint8_t  pub  = published.load(std::memory_order_acquire);

    // Выбрать свободный не-published слот.
    // По теореме при K=N+2 candidates всегда ненулевой.
    const uint32_t candidates = ~busy & ~(1u << pub);
    const uint8_t  j = static_cast<uint8_t>(__builtin_ctz(candidates));

    // Записать данные.
    // W-NoOverwritePublished: j != pub гарантировано выбором слота.
    slots[j] = value;

    // Атомарно переключить публикацию.
    published.store(j, std::memory_order_release);

    // Сигнал инициализации — идемпотентен, безопасен при повторных вызовах.
    initialized.store(true, std::memory_order_release);
}

// Reader
bool try_read(T& out) noexcept {
    // Шаг 1: проверить наличие данных.
    // После первой публикации этот load всегда возвращает true.
    if (!initialized.load(std::memory_order_acquire)) {
        return false;
    }

    // Шаг 2: загрузить опубликованный слот.
    const uint8_t i = static_cast<uint8_t>(
        published.load(std::memory_order_acquire));

    // Шаг 3: установить claim.
    // ПОРЯДОК КРИТИЧЕН: busy_mask до refcnt.
    busy_mask.fetch_or(1u << i, std::memory_order_acq_rel);
    refcnt[i].fetch_add(1, std::memory_order_acq_rel);

    // Шаг 4: читать данные.
    // Verify не нужен: W-NoOverwritePublished гарантирует стабильность slots[i].
    // Writer не может начать писать в i пока i == published,
    // а published сменится только после завершения записи в j != i.
    out = slots[i];

    // Шаг 5: снять claim.
    // ПОРЯДОК КРИТИЧЕН: refcnt до busy_mask.
    if (refcnt[i].fetch_sub(1, std::memory_order_acq_rel) == 1) {
        busy_mask.fetch_and(~(1u << i), std::memory_order_release);
    }
    return true;
}
```

---

## Инварианты (Safety)

### I1. Единоличная запись

Только writer выполняет записи в `slots[i]`, изменяет `published` и `initialized`.

### I2. Управление busy_mask и refcnt

Только reader'а изменяют `busy_mask` и `refcnt[i]`. Writer только **читает** `busy_mask` и `published` (acquire).

### I3. W-NoOverwritePublished

Writer выбирает слот `j` где `j != published` и `busy_mask[j] == 0`. Запись в `slots[j]` начинается только после этого выбора. `published` переключается на `j` только **после** завершения записи.

### I4. Чтение только под claim

Reader читает `slots[i]` только при удержании claim: между `fetch_or(1<<i)` и финальным `fetch_and(~(1<<i))`.

### I5. Порядок установки и снятия claim

**Установка** (строго в этом порядке):
1. `busy_mask.fetch_or(1<<i, acq_rel)` — writer видит занятость немедленно
2. `refcnt[i].fetch_add(1, acq_rel)` — фиксируем счётчик

**Снятие** (строго в этом порядке):
1. `val = refcnt[i].fetch_sub(1, acq_rel)` — уменьшаем счётчик
2. если `val == 1`: `busy_mask.fetch_and(~(1<<i), release)` — последний reader снимает бит

### I6. Стабильность published слота

Из I3 следует: пока `published == i`, writer не пишет в `slots[i]`. `slots[i]` стабилен на всё время между двумя последовательными `published.store`. Reader не нуждается в verify.

### I7. Монотонность initialized

`initialized` переходит из `false` в `true` ровно один раз — после первой публикации. Обратный переход невозможен. После установки все последующие `load(acquire)` возвращают `true`.

#### Замечание об ABA

Может ли writer дважды опубликовать один и тот же слот `i`? Да. Но в момент когда reader держит claim на `i` (`busy_mask[i]==1`, `refcnt[i]>0`) — writer не может выбрать `i` по I3. Данные в `slots[i]` стабильны на всё время чтения. ABA не нарушает корректности.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

`try_read(out)` возвращает `true` и заполняет `out` консистентной копией последнего опубликованного состояния — после первой публикации всегда.

### G2. Отсутствие torn read (структурное)

Невозможно пересечение «writer пишет `slots[i]`» и «reader читает `slots[i]`»: writer не пишет в `published` слот (I3), reader читает только `published` слот под claim (I4, I6).

### G3. Корректность при N reader'ах одного слота

`refcnt[i]` корректно отслеживает количество одновременных читателей. `busy_mask[i]` снимается только когда последний reader завершает чтение.

### G4. Latest-wins

Reader всегда получает последнее опубликованное состояние на момент `published.load(acquire)`.

### G5. No delivery guarantee

Промежуточные публикации могут быть перезаписаны.

### G6. Bounded WCET

* Writer: `load(busy_mask)` + `load(published)` + `ctz` + `copy(T)` + `store(published)` + `store(initialized)` — **фиксированное число операций**
* Reader: `load(initialized)` + `load(published)` + `fetch_or` + `fetch_add` + `copy(T)` + `fetch_sub` + (условно) `fetch_and` — **фиксированное число операций**

### G7. Progress

* Writer — **wait-free, O(1)**: `ctz(candidates)` детерминированно находит слот. По теореме `candidates` всегда ненулевой при K = N+2.
* Reader — **wait-free, O(1)**: фиксированная последовательность без ветвлений влияющих на число атомарных операций.

### G8. Отсутствие состояния NONE

`published` всегда содержит валидный индекс ∈ [0..K-1]. Нет окна недоступности данных после инициализации.

---

## Задержка свежести

| Компонент | Величина |
|---|---|
| Период публикации writer'а | `[0, T_w]` |
| Период опроса reader'а | `[0, T_r]` |
| Verify-fail | **отсутствует** |
| Аппаратная видимость | `~0..50 нс` |

**Worst-case: `T_w + T_r`**

Это оптимум для примитива с семантикой latest-wins и независимыми тактами writer'а и reader'а.

---

## Требования к типу T

```
std::is_trivially_copyable<T>::value == true
```

---

## Обязательные compile-time требования

* `N >= 1`, `K = N + 2`
* `K <= 32` при `uint32_t` маске; `K <= 64` при `uint64_t` маске (то есть N ≤ 30 и N ≤ 62 соответственно)
* `N <= 254` для `refcnt` типа `uint8_t`
* `std::atomic<bool>::is_always_lock_free == true`
* `std::atomic<uint8_t>::is_always_lock_free == true`
* `std::atomic<uint32_t>::is_always_lock_free == true`
* `std::is_trivially_copyable<T>::value == true`

*(Опционально)* `published`, `initialized` и `busy_mask` на отдельной cache line от `refcnt[]` и данных слотов.

*(Опционально)* Round-robin выбор среди `candidates` вместо `ctz` — равномерный износ слотов. Не влияет на корректность.

---

## Ограничения (Non-goals)

* Не очередь событий — доставка каждой публикации не гарантируется.
* N фиксировано на этапе компиляции.
* Не поддерживает более 30 (uint32_t) или 62 (uint64_t) одновременных reader'ов без смены типа `busy_mask`.
