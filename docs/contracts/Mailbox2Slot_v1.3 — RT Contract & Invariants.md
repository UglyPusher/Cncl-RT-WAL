# Mailbox2Slot (SPSC Snapshot Mailbox)

`docs/contracts/Mailbox2Slot.md` · Revision 1.4 — February 2026

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

> **Memory model:** Реализация обязана использовать `std::atomic<state_t>` (`state_t` — тип состояния, обычно `uint8_t`) из C++11 (или новее) с операциями `load(std::memory_order_acquire)` и `store(std::memory_order_release)`. Платформенные интринсики допустимы только если они предоставляют эквивалентные гарантии happens-before на целевой архитектуре. Использование `memory_order_relaxed` **запрещено** для операций, участвующих в happens-before цепочках между writer и reader.

* `pub_state` и `lock_state` — atomics **lock-free** на целевой платформе
* `pub_state.store(..., release)`, `pub_state.load(..., acquire)`
* `lock_state.store(..., release)`, `lock_state.load(..., acquire)`
* **Исключение:** `pub_state.load(relaxed)` в I5-ветке writer'а допустим, так как writer является единственным **пишущим** (`single-writer`) для `pub_state`; это read-my-own-write без необходимости дополнительного упорядочивания с reader'ом (упорядочивание уже обеспечено предшествующим `lock_state.load(acquire)`).

---

## Инициализация

После создания объекта:

* `pub_state` должен быть установлен в `NONE`
* `lock_state` должен быть установлен в `UNLOCKED`
* Содержимое слотов `S[0]` и `S[1]` не определено до первой публикации

```cpp
// init:
pub_state  = NONE      // 2
lock_state = UNLOCKED  // 2
```

Reader, вызывающий `try_read` до первой публикации, всегда получает `false`.

---

## API семантика

### Writer

`publish(value)` — публикует новое состояние. Внутренний алгоритм:

1. **Критическая секция** (preemption disabled):
   a. Однократно прочитать `lock_state` (acquire) и выбрать слот `j != lock_state`.
   b. Если `pub_state == j` — выполнить invalidate: `pub_state = NONE` (release).
   c. Снять запрет вытеснения.
2. **Вне критической секции:** записать данные в `S[j]`, опубликовать `pub_state = j` (release).

Допускается overwrite: writer может обновлять один и тот же слот многократно, пока reader держит другой.

#### Обоснование границ критической секции

Критическая секция защищает **только** шаги (a) и (b) — выбор слота и его отметку как недоступного. Запись данных `copy(T)` намеренно вынесена за границу.

**Инвариант на выходе из критической секции:** `pub_state != j` или `pub_state == NONE`. Любой reader, пришедший после `sys_preemption_enable()`, не найдёт слот `j` опубликованным и не сможет начать claim на него. Следовательно, запись `S[j]` безопасна без защиты от вытеснения.

**WCET критической секции** не зависит от `sizeof(T)` — содержит только атомарные операции.

### Reader

`try_read(out)`:

* делает bounded attempt получить консистентный снапшот
* если не получилось (NONE или гонка публикации) → возвращает `false`; reader продолжает работать со старым состоянием (sticky state)
* **retry отсутствует** — при отказе reader уходит на следующий тик

Claim-verify (steps 1–4) выполняется в критической секции (preemption disabled): три атомарные операции, не зависит от `sizeof(T)`. Устраняет false-возвраты, вызванные вытеснением writer'ом в окне между p1 и p2. `copy(T)` вынесен за границу критической секции — симметрично с writer'ом.

#### Постусловие `try_read`

> По завершении `try_read` (независимо от возвращаемого значения — `true` или `false`) выполняется `lock_state == UNLOCKED`. Это гарантирует что lock никогда не «зависнет», в том числе при fail по `p2 ≠ p1`.

---

## Псевдокод

```cpp
// init
pub_state  = NONE      // 2
lock_state = UNLOCKED  // 2

// Writer
void publish(const T& value) {
    // --- критическая секция: выбор слота + invalidate ---
    sys_preemption_disable();

    // Шаг 1: выбор слота.
    // acquire: happens-before с reader's lock_state.store(release).
    int locked = lock_state.load(std::memory_order_acquire);
    int j = (locked == 1) ? 0 : 1;  // при UNLOCKED(2) даёт 1 — допустимо

    // Шаг 2: invalidate если слот j уже опубликован (I5).
    // relaxed: writer — единственный пишущий (single-writer) pub_state; read-my-own-write.
    // Гонки с reader'ом здесь нет: pub_state == j => lock_state != j.
    if (pub_state.load(std::memory_order_relaxed) == j) {
        pub_state.store(NONE, std::memory_order_release);
    }

    sys_preemption_enable();
    // --- конец критической секции ---
    // Инвариант: pub_state != j  OR  pub_state == NONE.
    // Reader не может начать claim на j до pub_state.store(j) ниже.

    // Шаг 3: запись данных и публикация (вне критической секции).
    S[j] = value;
    pub_state.store(j, std::memory_order_release);
}

// Reader
bool try_read(T& out) {
    // --- критическая секция: claim-verify (3 атомарные операции) ---
    sys_preemption_disable();

    int p1 = pub_state.load(std::memory_order_acquire);
    if (p1 == NONE) {
        sys_preemption_enable();
        // lock_state уже UNLOCKED по постусловию предыдущего вызова
        return false;
    }
    lock_state.store(p1, std::memory_order_release);
    int p2 = pub_state.load(std::memory_order_acquire);

    sys_preemption_enable();
    // --- конец критической секции ---
    // Инвариант: claim установлен (lock_state == p1) или уже снят (NONE-путь).
    // copy(T) вне критической секции — симметрично с writer'ом.

    if (p2 != p1) {
        lock_state.store(UNLOCKED, std::memory_order_release);
        return false;
    }
    out = S[p1];  // копирование вне критической секции
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

Writer читает `lock_state` (acquire) под защитой от вытеснения, выбирает `j != lock_state`, после чего снимает защиту. Повторная проверка `lock_state` после записи данных не требуется — к этому моменту инвариант на выходе из критической секции гарантирует, что reader не может начать claim на `j` (дополнительная защита обеспечивается verify-шагом reader'а, I6).

Формально: writer **не начинает** запись в слот `i`, если на момент принятия решения `lock_state == i`.

### I4. Чтение только залоченного слота

Reader читает `S[i]` **только** если `lock_state == i` (claim удерживается на всё время чтения).

### I5. Запрет записи в опубликованный слот без invalidate

Если writer собирается писать в слот `j` и в данный момент `pub_state == j`, writer обязан (в пределах критической секции):

1. `pub_state = NONE` (release)

После выхода из критической секции:

2. записать данные в `S[j]`
3. `pub_state = j` (release)

**Цель:** исключить начало чтения reader'ом во время записи. После шага 1 reader, пришедший в любой момент до шага 3, увидит `pub_state == NONE` и вернёт `false`.

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
2. Writer читает `lock_state` с `acquire` в критической секции — по правилу happens-before (release → acquire) writer **гарантированно видит** `lock_state == p1`.
3. По I3 writer не начинает запись в слот `p1`, пока `lock_state == p1`.
4. Следовательно, данные в `S[p1]` не изменялись с момента первоначальной публикации.
5. При `p1 == p2` reader читает консистентный снапшот.

Безопасность ABA обеспечивается комбинацией I3, happens-before между `store(release)` reader'а и `load(acquire)` writer'а, **и** атомарностью критических секций обоих участников на вытесняемых системах.

### Лемма: Safe Slot Availability

> Writer всегда имеет доступный слот для записи (из I2 и I3). Поскольку reader единственный и не может держать более одного lock одновременно, в любой момент залочен не более одного слота. Writer выбирает слот `j != lock_state` (или любой при `lock_state == UNLOCKED`). Слот `j` никогда не залочен, запись в него всегда легальна по I3. Блокировка writer'а невозможна; G6 (wait-free) выполняется безусловно.

---

## Гарантии (Guarantees)

### G1. Консистентность снапшота

Если `try_read(out)` возвращает `true`, то `out` является **консистентной копией** некоторого состояния `T`, которое было опубликовано writer'ом.

### G2. Отсутствие torn read

Невозможно пересечение «writer пишет `S[i]`» и «reader читает `S[i]`» при соблюдении инвариантов I3–I6 **и** платформенных требований (§Preemption Safety). На SMP эта гарантия не достижима средствами стандарта C++ — см. §Known Limitations.

### G3. Latest-at-claim (freshness)

При успешном `try_read` reader получает состояние, опубликованное writer'ом не позднее момента `lock_state.store(p1, release)` (шаг 3 в I6). Состояния, опубликованные после этого момента, могут быть не отражены — это допустимо по семантике latest-wins.

### G4. No delivery guarantee

Не гарантируется, что reader увидит каждую публикацию. Промежуточные публикации могут быть перезаписаны.

### G5. Bounded WCET

* Writer (критическая секция): фиксированное число атомарных операций; не зависит от `sizeof(T)`.
* Writer (полный `publish`): критическая секция + `write(T)` + одна атомарная публикация.
* Reader (критическая секция): три атомарные операции (claim-verify); не зависит от `sizeof(T)`.
* Reader (полный `try_read`): критическая секция + `copy(T)` + release; без бесконечных циклов.

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

`pub_state` и `lock_state` обёрнуты в `CachelinePadded<A>` — шаблон, выравнивающий atomic по границе cacheline и добавляющий padding до полного размера cacheline. Корректность layout подтверждается:

```cpp
static_assert(sizeof(CachelinePadded<std::atomic<uint8_t>>) == SYS_CACHELINE_BYTES);
static_assert(sizeof(Slot) % SYS_CACHELINE_BYTES == 0);
static_assert(sizeof(Mailbox2SlotCore<T>) == 2*sizeof(Slot) + 2*sizeof(CachelinePadded<...>));
```

Это гарантирует отсутствие false sharing между слотами и между контрольными словами — не как намерение, а как compile-time факт.

---

## Preemption Safety

### Вытесняемые однопроцессорные системы

На системах с вытеснением (RTOS, Linux PREEMPT_RT, bare-metal с IRQ на writer-контексте) writer обязан защитить блок **выбора слота + invalidate** от вытеснения.

**Обоснование:** без защиты возможен сценарий:

1. Writer читает `lock_state == UNLOCKED`, выбирает `j`.
2. Writer вытесняется прерыванием.
3. Reader выполняет `try_read`, залочивает тот же слот `j`, читает `S[j]`.
4. Writer возобновляется со stale `locked`, пишет в `S[j]` — **torn read**.

Критическая секция охватывает только атомарные операции выбора и invalidate. `copy(T)` вынесен за её пределы — **WCET прерываний не зависит от `sizeof(T)`**.

**Платформенные механизмы** (`sys/sys_preemption.hpp`):

| Платформа | Механизм |
|---|---|
| bare-metal ARM Cortex-M | `__disable_irq()` / `__enable_irq()` |
| FreeRTOS | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` |
| RTEMS | `rtems_interrupt_local_disable/enable()` |
| Linux PREEMPT_RT | `local_irq_save()` / `local_irq_restore()` |

### Reader

`try_read()` защищает claim-verify (steps 1–4) аналогичной критической секцией: три атомарные операции, не зависит от `sizeof(T)`. Устраняет false-возвраты вызванные вытеснением writer'ом в окне между загрузкой `p1` и проверкой `p2`.

### Невытесняемые системы / cooperative scheduling

На системах без вытеснения (cooperative RTOS, bare-metal superloop без IRQ на writer-контексте) критические секции технически не требуются. `sys_preemption_disable/enable` реализуются как no-op.

---

## Ограничения (Non-goals и Known Limitations)

### Не очередь событий

Примитив не гарантирует доставку каждого обновления.

### Не поддерживает более одного reader

Расширение требует refcount/epochs/3+ слота.

### Указатель на слот

Чтение «по месту» допустимо только внутри claim/release; хранение указателя на слот после release запрещено.

### Known Limitation: torn read на SMP

На многопроцессорных системах (SMP) запрет вытеснения на одном ядре не предотвращает параллельный доступ writer'а и reader'а к одному слоту с другого ядра.

**Сценарий:**

1. Writer (ядро 0) завершает критическую секцию: выбрал `j = kSlot1`, выполнил invalidate (`pub_state = NONE`).
2. Writer начинает запись `S[1]`.
3. Reader (ядро 1) одновременно видит `pub_state == NONE` → `false`... но возможен вариант, когда reader успел залочить `kSlot1` до invalidate writer'а и прошёл verify.
4. Writer пишет в `S[1]`, reader читает из `S[1]` — конкурентный доступ к не-атомарному `T`.

По C++ memory model это **undefined behavior**.

**Корректность на практике** обеспечивается платформенными гарантиями когерентности cacheline на ARM Cortex и x86-64 для `sizeof(T) ≤ SYS_CACHELINE_BYTES` и выровненных данных. Это **платформенная гарантия**, выходящая за пределы стандарта C++.

**Формальная верификация** по стандарту C++ невозможна без атомизации `T`, что несовместимо с RT-контрактом данного примитива. Использование `Mailbox2Slot` на SMP — осознанный выбор с принятием этого ограничения.

---

## Changelog

| Revision | Дата | Изменения |
|---|---|---|
| 1.0 | — | Начальная версия |
| 1.1 | — | Уточнение ABA-доказательства |
| 1.2 | — | Добавлен раздел G3, уточнена формулировка I5 |
| 1.3 | Feb 2026 | Уточнение memory_order; добавлена Лемма Safe Slot Availability |
| 1.4 | Feb 2026 | Критические секции в `publish()` и `try_read()` для вытесняемых систем; `pub_state.load(relaxed)` в I5 с обоснованием; `CachelinePadded<>` wrapper + `static_assert` на layout; `sole owner` → `single-writer`; `std::atomic<T>` → `std::atomic<state_t>` в memory model; §Preemption Safety (writer + reader); §Known Limitations (SMP torn read); уточнена G2, G3, G5; init-блок в псевдокод |
