# Mailbox2Slot (SPSC Snapshot Mailbox)

`docs/contracts/Mailbox2Slot.md` · Revision 1.3 — February 2026

---

## Назначение

Примитив передачи **снапшота состояния** от одного писателя к одному читателю.

**Семантика: latest-wins.** Промежуточные публикации могут быть потеряны.

---

## Модель

### Участники

* **Writer**: ровно один поток/ядро, только он пишет в слоты и меняет `pub_state`.
* **Reader**: ровно один поток/ядро, только он читает и меняет `lock_state`.

### Память

* 2 слота: `S[0]`, `S[1]` типа `T`
* Атомарные состояния:
  * `pub_state ∈ {0,1,2}`: опубликован слот 0/1 или **NONE(2)**
  * `lock_state ∈ {0,1,2}`: reader держит слот 0/1 или **UNLOCKED(2)**

### Атомики и барьеры

> **Memory model:** Реализация обязана использовать `std::atomic<T>` из C++11 (или новее) с операциями `load(std::memory_order_acquire)` и `store(std::memory_order_release)`. Платформенные интринсики допустимы только если они предоставляют эквивалентные гарантии happens-before на целевой архитектуре. Использование `memory_order_relaxed` **запрещено** для `pub_state` и `lock_state`.

* `pub_state` и `lock_state` — atomics **lock-free** на целевой платформе
* `pub_state.store(..., release)`, `pub_state.load(..., acquire)`
* `lock_state.store(..., release)`, `lock_state.load(..., acquire)` (для writer-side проверки)

---

## Инициализация

После создания объекта:

* `pub_state` должен быть установлен в `NONE`
* `lock_state` должен быть установлен в `UNLOCKED`
* Содержимое слотов `S[0]` и `S[1]` не определено до первой публикации

Reader, вызывающий `try_read` до первой публикации, всегда получает `false`.

---

## API семантика

### Writer

`publish(value)` — публикует новое состояние. Внутренний алгоритм:

1. Однократно прочитать `lock_state` (acquire) и выбрать слот `j != lock_state`. Повторная проверка не требуется — защита обеспечивается verify-шагом reader'а (I6).
2. Если `pub_state == j` — выполнить invalidate согласно I5, затем записать данные и опубликовать.
3. Иначе — записать данные и опубликовать напрямую.

Допускается overwrite: writer может обновлять один и тот же слот многократно, пока reader держит другой.

### Reader

`try_read(out)`:

* делает bounded attempt получить консистентный снапшот
* если не получилось (NONE или гонка публикации) → возвращает `false`; reader продолжает работать со старым состоянием (sticky state)
* **retry отсутствует** — при отказе reader уходит на следующий тик

#### Постусловие `try_read`

> По завершении `try_read` (независимо от возвращаемого значения — `true` или `false`) выполняется `lock_state == UNLOCKED`. Это гарантирует что lock никогда не «зависнет», в том числе при fail по `p2 ≠ p1`.

---

## Псевдокод

```cpp
// Writer
void publish(const T& value) {
    // Шаг 1: выбор слота — однократно, без повторной проверки.
    // lock_state читается с acquire: если reader ранее сделал store(release),
    // этот load гарантированно увидит актуальное значение (happens-before).
    int locked = lock_state.load(std::memory_order_acquire);
    int j = (locked == 1) ? 0 : 1;  // при UNLOCKED(2) даёт 1 — допустимо

    // Шаг 2: invalidate если слот j уже опубликован (I5).
    // Гонки с reader'ом здесь нет: если pub_state == j, то lock_state != j
    // (иначе writer выбрал бы другой слот), значит reader не может
    // начать claim на j в этот момент.
    if (pub_state.load(std::memory_order_acquire) == j) {
        pub_state.store(NONE, std::memory_order_release);
    }

    // Шаг 3: запись данных и публикация
    S[j] = value;
    pub_state.store(j, std::memory_order_release);
}

// Reader
bool try_read(T& out) {
    int p1 = pub_state.load(std::memory_order_acquire);
    if (p1 == NONE) {
        // lock_state уже UNLOCKED по постусловию предыдущего вызова
        return false;
    }
    lock_state.store(p1, std::memory_order_release);
    int p2 = pub_state.load(std::memory_order_acquire);
    if (p2 != p1) {
        lock_state.store(UNLOCKED, std::memory_order_release);
        return false;
    }
    out = S[p1];  // копирование под защитой claim
    lock_state.store(UNLOCKED, std::memory_order_release);
    return true;
}
```

---

## Инварианты (Safety)

### I1. Единоличная запись

Только writer выполняет записи в `S[i]` и изменяет `pub_state`.

### I2. Единоличное владение lock

Только reader **изменяет** `lock_state`. Writer вправе **читать** `lock_state` (для выбора слота), но не изменяет его.

### I3. Запрет записи в залоченный слот

Writer однократно читает `lock_state` (acquire), выбирает `j != lock_state`, и только после этого начинает запись в `S[j]`. Повторная проверка после начала записи не требуется — защита обеспечивается verify-шагом reader'а (I6).

Формально: writer **не начинает** запись в слот `i`, если на момент принятия решения `lock_state == i`.

### I4. Чтение только залоченного слота

Reader читает `S[i]` **только** если `lock_state == i` (claim удерживается на всё время чтения).

### I5. Запрет записи в опубликованный слот без invalidate

Если writer собирается писать в слот `j` и в данный момент `pub_state == j`, writer обязан:

1. `pub_state = NONE` (release)
2. записать данные в `S[j]`
3. `pub_state = j` (release)

**Цель:** исключить начало чтения reader'ом во время записи.

### I6. Claim-verify у reader (защита от «stale pub»)

Reader обязан подтверждать, что публикация не изменилась при захвате:

1. `p1 = pub_state.load(acquire)`
2. если `p1 == NONE` → `return false` *(lock_state уже UNLOCKED по постусловию)*
3. `lock_state.store(p1, release)`
4. `p2 = pub_state.load(acquire)`
5. если `p2 ≠ p1` → `lock_state = UNLOCKED`, `return false` *(без retry)*

Только если `p1 == p2` reader имеет право читать `S[p1]`.

#### Замечание об ABA

Возможен ли сценарий, когда `pub_state` изменилось и вернулось к исходному значению между шагами 1 и 4 (`p1 == p2`, но данные «чужие»)?

Нет. ABA исключён структурно. Цепочка рассуждений:

1. Reader выполнил `lock_state.store(p1, release)` на шаге 3.
2. Writer читает `lock_state` с `acquire` перед записью — по правилу happens-before (release → acquire) writer **гарантированно видит** `lock_state == p1`.
3. По I3 writer не начинает запись в слот `p1`, пока `lock_state == p1`.
4. Следовательно, данные в `S[p1]` не изменялись с момента первоначальной публикации.
5. При `p1 == p2` reader читает консистентный снапшот.

Безопасность ABA обеспечивается комбинацией I3 и happens-before между `store(release)` reader'а и `load(acquire)` writer'а.

### Лемма: Safe Slot Availability

> Writer всегда имеет доступный слот для записи (из I2 и I3). Поскольку reader единственный и не может держать более одного lock одновременно, в любой момент залочен не более одного слота. Writer выбирает слот `j != lock_state` (или любой при `lock_state == UNLOCKED`). Слот `j` никогда не залочен, запись в него всегда легальна по I3. Блокировка writer'а невозможна; G6 (wait-free) выполняется безусловно.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

Если `try_read(out)` возвращает `true`, то `out` является **консистентной копией** некоторого состояния `T`, которое было опубликовано writer'ом.

### G2. Отсутствие torn read

Невозможно пересечение «writer пишет `S[i]`» и «reader читает `S[i]`», при соблюдении инвариантов I3–I6.

### G3. Latest-at-claim (freshness)

При успешном `try_read` reader получает состояние, опубликованное writer'ом не позднее момента `load(p2)` в шаге verify (шаг 4 в I6), при условии `p1 == p2`. Состояния, опубликованные после `load(p2)`, могут быть не отражены — это допустимо по семантике latest-wins.

### G4. No delivery guarantee

Не гарантируется, что reader увидит каждую публикацию. Промежуточные публикации могут быть перезаписаны.

### G5. Bounded WCET

* Writer (без invalidate): `write(T)` + `publish` — фиксированное число операций
* Writer (с invalidate по I5): `NONE` + `write(T)` + `publish` — фиксированное число операций
* Reader: фиксированное число атомарных операций + `copy(T)`; без бесконечных циклов

### G6. Progress

При корректной платформенной реализации атомиков:

* Writer — **wait-free** (не ждёт reader; см. Лемму Safe Slot Availability)
* Reader — **wait-free** для `try_read` (retry отсутствует)

---

## Требования к типу T

```
std::is_trivially_copyable<T>::value == true
```

Копирование `T` выполняется под защитой claim (`lock_state`) простым присваиванием или `std::memcpy`. Нетривиальные конструкторы копирования, виртуальные функции и синхронизирующие примитивы внутри `T` недопустимы в RT-контексте.

---

## Обязательные compile-time требования

* `std::atomic<state_t>::is_always_lock_free == true` (state_t обычно `uint8_t`)
* `sizeof(state_t) == 1` (или другая нативная lock-free ширина, разрешённая в `sys_config`)
* `std::is_trivially_copyable<T>::value == true`

*(Опционально)* разделение `pub_state` и `lock_state` по cache line на SMP для снижения jitter — не влияет на корректность.

---

## Ограничения (Non-goals)

* Не очередь событий, не гарантирует доставку каждого обновления.
* Не поддерживает более одного reader без расширения протокола (refcount/epochs/3+ слота).
* Чтение «по месту» допустимо только внутри claim/release; хранение указателя на слот после release запрещено.
