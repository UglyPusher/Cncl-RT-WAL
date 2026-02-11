# SPSCRing — RT Contract & Invariants

## 0. Scope

`SPSCRing<T, Capacity>` — **Single-Producer / Single-Consumer** кольцевой буфер фиксированного размера для передачи элементов типа `T` между двумя контекстами исполнения:

* **Producer**: один поток/контекст записи (включая hard-RT).
* **Consumer**: один поток/контекст чтения (обычно non-RT).

Компонент предназначен для **bounded, deterministic, lossy publication** данных со стороны producer:

* без блокировок
* без системных вызовов
* с **ограниченной ёмкостью = Capacity − 1**
* с возможной **потерей данных при переполнении** (`push()` возвращает `false`)

Очередь **не гарантирует доставку**.

---

## 1. Compile-time invariants

1. `Capacity` является степенью двойки и `Capacity ≥ 2`:

   ```
   Capacity >= 2 && (Capacity & (Capacity - 1)) == 0
   ```

2. `T` удовлетворяет `std::is_trivially_copyable_v<T>`:

   * отсутствуют скрытые аллокации
   * отсутствуют деструкторы
   * стоимость копирования **bounded и детерминирована**

---

## 2. Runtime invariants

### Обозначения

* `head` — индекс следующей позиции записи (**producer-owned**)
* `tail` — индекс следующей позиции чтения (**consumer-owned**)
* индексы лежат в диапазоне `[0, Capacity)`
* обновление выполняется по маске `(Capacity − 1)`

### Инварианты

1. **Single-writer rule**

   * `head_` модифицируется только producer
   * `tail_` модифицируется только consumer

2. **Slot ownership**

   * слот `buffer_[i]` записывается только producer
   * читается только consumer
   * корректность перехода владения обеспечивается публикацией через `head_`

3. **No overwrite rule**

   Producer не перезаписывает непрочитанный слот:

   ```
   push() == false  ⇔  next_head == tail
   ```

4. **No read-of-unpublished rule**

   Consumer не читает неопубликованный слот:

   ```
   pop() == false  ⇔  tail == head
   ```

### Usable capacity

Максимальное число элементов в очереди:

```
Capacity − 1
```

Один слот всегда зарезервирован для различения **empty/full**.

---

## 3. Threading model requirements (preconditions)

1. Ровно **один producer** и **один consumer** на экземпляр `SPSCRing`.
2. `push()` вызывается только producer-контекстом.
3. `pop()` вызывается только consumer-контекстом.
4. Producer не должен быть реентерабельным (например, вложенные IRQ/NMI не должны одновременно вызывать `push()`).
5. Нарушение условий ⇒ **undefined behavior** в рамках контракта компонента.
6. Consumer не должен быть реентерабельным (вложенные IRQ/NMI не должны одновременно вызывать pop()).
---

## 4. Memory ordering / happens-before semantics

### Publication rule (producer → consumer)

Producer:
 - вычисляет next_head
 - читает tail_ (acquire) и проверяет full
 - при full: возвращает false, не модифицируя состояние
 - записывает buffer_[head]
 - head_.store(next_head, release)

Consumer:
 - head_.load(acquire) и проверяет empty
 - читает buffer_[tail]
 - tail_.store(next_tail, release)

**Гарантия:**
если `pop()` наблюдает обновлённый `head_` (acquire),
он **обязательно** видит соответствующую запись в `buffer_`.

---

### Consumption rule (consumer → producer)

Consumer публикует освобождение слота:

```
tail_.store(next_tail, std::memory_order_release)
```

Producer проверяет:

```
tail_.load(std::memory_order_acquire)
```

**Гарантия:**
наблюдение обновлённого `tail_` означает,
что слот безопасно переиспользовать.

---

### Linearization points

* **push** линейризуется в `head_.store(release)`
* **pop** линейризуется в `tail_.store(release)`

Owner-side reads of head_ and tail_ may use memory_order_relaxed,
as no cross-thread synchronization is required for owned indices.
---

## 5. RT contract (producer side)

`push()` удовлетворяет hard-RT требованиям:

### Guarantees

* **Bounded execution time**: O(1), без циклов ожидания и CAS-повторов
* **No blocking / waiting**: нет mutex/spin/condvar
* **No allocation**: нет dynamic allocation
* **No syscalls / IO**
* **Bounded failure**: при переполнении возвращает `false`
* **No side effects on failure**

### Explicit non-guarantees

* обработка consumer’ом **не гарантируется**
* при переполнении данные **могут быть потеряны**

push() is wait-free for the producer under the single-producer precondition.
---

## 6. Non-RT contract (consumer side)

`pop()`:

* bounded
* lock-free
* может использоваться в RT, но **не позиционируется** как hard-RT API
  из-за возможной последующей non-RT обработки данных.

---

## 7. Telemetry APIs

`empty()` и `full()`:

* **empty()/full() may use relaxed memory reads and do not establish happens-before edges. It is forbidden to use their return values for synchronization or safety decisions about publication/consumption.**
* **Telemetry only; not for synchronization**

Могут возвращать устаревшие значения:

* `empty()` может быть `true`, когда элемент уже опубликован
* `full()` может быть `false`, когда очередь уже заполнена

empty()/full() могут использовать relaxed-чтения и не образуют happens-before; запрещено использовать их для синхронизации или принятия решений о безопасности публикации.
---

## 8. Cache / layout invariants

Цель — **снижение jitter**, не корректность.

1. `head_` и `tail_` размещены на отдельных cache-line
   (`alignas(SYS_CACHELINE_BYTES)`)

2. Между control-полями и `buffer_` есть:

```
pad[SYS_CACHELINE_BYTES]
```

→ разделение **control / data**

3. `buffer_` выровнен по cache-line
   → фиксированная геометрия начала массива

Это **non-functional performance invariant**.

On cache-coherent architectures, the layout eliminates false sharing
between producer-written and consumer-written cache lines.

---

## 9. Error model

Единственные ошибки API:

* `push() == false` → очередь полна
* `pop() == false` → очередь пуста

If push() returns false, neither buffer_ nor head_ are modified.
If pop() returns false, neither tail_ nor the output value are modified.

Гарантии:

* **нет частичных записей**
* **нет повреждения состояния**
* **нет побочных эффектов при false**
* **исключения отсутствуют (`noexcept`)**

---

## 10. Extension policy (non-RT only)

Расширения (например, batch-drain):

* реализуются **поверх** `SPSCRing`
* не должны изменять RT-семантику `push()`
* не должны добавлять работу в **producer RT-path**

Базовый примитив остаётся:

> **минимальным, формально проверяемым, RT-детерминированным.**


## 11. Progress guarantees

Данный раздел формализует **гарантии продвижения (progress guarantees)** для операций `push()` и `pop()` в терминах классической модели неблокирующих алгоритмов.

### 11.1 Producer progress

При соблюдении preconditions раздела 3:

* существует **ровно один producer**
* `push()` не вызывается реентерабельно

операция:

```
push()
```

обладает свойством:

> **wait-free для producer**

Это означает:

* время выполнения **строго ограничено константой**
* отсутствуют:

  * циклы повторных попыток (retry loops)
  * CAS-конфликты
  * зависимость от действий других потоков
* завершение операции гарантировано **за конечное число шагов**,
  независимо от состояния consumer (включая его остановку).

Следствие:

* `push()` удовлетворяет требованиям **hard real-time bounded progress**.

---

### 11.2 Consumer progress

При соблюдении preconditions раздела 3:

* существует **ровно один consumer**
* `pop()` не вызывается реентерабельно

операция:

```
pop()
```

обладает свойством:

> pop() is wait-free in completion: every call finishes in a bounded number of steps without waiting or retry.

Это означает:

* система в целом гарантирует продвижение:

  * либо текущий `pop()` завершается успешно/с отказом,
  * либо продвижение обеспечивается будущими вызовами `pop()` или `push()`.
* отсутствуют:

  * взаимные блокировки
  * ожидание освобождения mutex/spinlock
  * зависимость от планировщика ОС.

`pop()` также имеет:

* **bounded execution time**
* отсутствие неограниченных циклов ожидания

Однако `pop()` не классифицируется как wait-free, поскольку:

* успешное извлечение элемента
  зависит от действий producer
  (наличия опубликованных данных).

---

### 11.3 System-level progress property

Для системы:

```
Single producer + single consumer + SPSCRing
```

гарантируется:

> **The system is non-blocking: neither side waits for the other; each call completes in bounded time. If one side stops, the other continues to complete calls (possibly with false).**

В частности:

* producer никогда не блокируется действиями consumer
* consumer никогда не блокируется действиями producer
* остановка одной стороны:

  * не приводит к зависанию другой
  * влияет только на **успешность**, но не на **завершимость** операций

Это свойство является ключевым для:

* hard-RT публикации событий
* безопасной деградации при остановке non-RT consumer
* построения bounded lossy-logging архитектур.

## 12. Misuse scenarios and undefined behavior

Данный раздел фиксирует **запрещённые сценарии использования**, нарушение которых выводит компонент за пределы формального контракта и приводит к **undefined behavior** на уровне модели компонента (независимо от того, проявится ли это как наблюдаемая ошибка на практике).

---

### 12.1 Multiple producers

Использование более чем одного producer-контекста для одного экземпляра `SPSCRing` запрещено.

Нарушение приводит к:

* гонке записи `head_`
* потере элементов
* нарушению инварианта **single-writer rule**
* разрушению happens-before публикации

Даже если записи выполняются «редко», корректность **не гарантируется**.

---

### 12.2 Multiple consumers

Использование более чем одного consumer-контекста запрещено.

Нарушение приводит к:

* гонке записи `tail_`
* двойному чтению или потере элементов
* нарушению линейризуемости `pop()`

---

### 12.3 Re-entrant producer (ISR / NMI nesting)

Повторный вход в `push()` из вложенных прерываний того же producer-контекста запрещён.

Последствия:

* конкурентная модификация `head_`
* частичная публикация элемента
* потеря wait-free гарантии

Это относится к:

* вложенным IRQ
* NMI поверх IRQ
* любым формам асинхронного повторного вызова `push()`

---

### 12.4 Re-entrant consumer

Аналогично producer, повторный вход в `pop()` запрещён.

Последствия:

* конкурентная запись `tail_`
* потеря или дублирование элементов
* нарушение инвариантов очереди

---

### 12.5 Non-trivially-copyable `T`

Использование типа `T`, не удовлетворяющего `std::is_trivially_copyable_v<T>`, запрещено.

Возможные последствия:

* скрытые аллокации
* вызовы конструкторов/деструкторов
* небounded время выполнения
* нарушение hard-RT контракта
* частично сконструированные объекты при гонках

Compile-time проверка предотвращает этот сценарий,
но её удаление или обход выводит систему из области корректности.

---

### 12.6 Использование `empty()` / `full()` для синхронизации

Запрещено использовать:

```
empty()
full()
```

для:

* принятия решений о публикации
* управления потоками
* реализации протоколов синхронизации

Причина:

* relaxed-чтения
* отсутствие happens-before
* возможные устаревшие значения в **обе стороны**

Следствие:

> корректная логика должна опираться **только на `push()` / `pop()`**.

---

### 12.7 Игнорирование результата `push()`

Игнорирование возвращаемого значения:

```
push() == false
```

означает **необработанную потерю данных**.

Это допустимо только если:

* система спроектирована как **lossy**
* потери явно учтены архитектурно
* существуют метрики/сигналы деградации

В противном случае это является **архитектурной ошибкой уровня системы**.

---

### 12.8 Использование после разрушения consumer

Если consumer окончательно остановлен, а producer продолжает:

* `push()` остаётся wait-free
* но очередь рано или поздно станет полной
* все последующие публикации будут теряться

Это **ожидаемое bounded-loss поведение**,
но его необходимо учитывать в shutdown-протоколах системы.

---

### 12.9 Нарушение cache-coherent предположений

Контракт предполагает:

* cache-coherent память
* корректную работу acquire/release атомиков

Использование на:

* некогерентных DMA-областях
* memory-mapped регионах без барьеров
* нестандартных memory-model платформах

требует **дополнительной валидации** вне данного контракта.

Use with DMA or non-coherent memory requires explicit cache maintenance and memory barriers outside this contract.
---

## 12.10 Summary

Все перечисленные сценарии:

* **не поддерживаются компонентом**
* **не обязаны детектироваться**
* выводят систему за пределы формально доказанной корректности

Следовательно:

> корректность `SPSCRing` гарантируется
> **только при строгом соблюдении разделов 1–3 настоящего контракта.**
