Ниже — формализованный документ в стиле твоего SPSC. Коротко, строго, без воды.

---

# TaskWrapper — RT Contract & Invariants

## 0. Scope

`TaskWrapper<Payload>` — минимальный адаптер вызова RT-задачи.

Назначение:

* обеспечить формальный вход в RT-домен
* задать compile-time контракт `step(uint32_t)`
* опубликовать completion heartbeat

Wrapper **не является**:

* scheduler’ом
* lifecycle manager’ом
* deadline monitor’ом
* механизмом синхронизации между задачами

---

## 1. Compile-time contract

`Payload` обязан удовлетворять:

```
void step(uint32_t) noexcept
```

Формально:

```
requires(Payload& p, uint32_t t) {
    { p.step(t) } noexcept -> std::same_as<void>;
}
```

Следствия:

* отсутствие исключений
* отсутствие необходимости try/catch
* детерминированная сигнатура вызова

---

## 2. Execution model

### 2.1 Single-entry rule

Для одного экземпляра `TaskWrapper`:

> В любой момент времени допускается ровно один активный вызов `step()`.

Запрещено:

* параллельный вызов из разных потоков
* вложенный вызов через nested IRQ
* повторный вход до завершения предыдущего

Нарушение → undefined behavior на уровне компонента.

---

### 2.2 Scheduler preconditions

Scheduler обязан гарантировать:

1. Non-reentrant invocation.
2. Отсутствие параллельного вызова одного и того же wrapper.
3. Монотонный или циклический `now` (допускается переполнение uint32_t).
4. Вызов `step()` в соответствии с заданной политикой периода.

Wrapper не проверяет эти условия.

---

## 3. Heartbeat semantics

После успешного завершения `step(now)` выполняется:

```
hb.store(now, memory_order_release);
```

Семантика:

> `hb` = last successfully completed tick.

---

### 3.1 Completion linearization point

Линейризация выполнения задачи происходит в момент:

```
hb.store(now, release)
```

Если другой поток выполняет:

```
hb.load(memory_order_acquire)
```

то гарантируется:

* все записи, выполненные внутри `step()`,
  происходят-before чтения heartbeat.

---

### 3.2 Detection latency

Если `step()` зависает:

* heartbeat не обновляется
* система обнаруживает это через ≥ 1 период

Bounded detection latency:

```
≤ period + jitter (если monitor проверяет каждый тик)
≤ monitor_interval + period + jitter (для периодического monitor)
```

---

## 4. Real-time guarantees (wrapper level)

Wrapper гарантирует:

* O(1) выполнение `step()` (без скрытых циклов)
* отсутствие блокировок
* отсутствие динамических аллокаций
* отсутствие syscalls
* отсутствие скрытых CAS
* отсутствие runtime-ветвлений (кроме if constexpr)

---

## 5. Non-guarantees

Wrapper **не гарантирует**:

* соблюдение дедлайна
* отсутствие jitter
* правильность scheduler
* thread-safety Payload
* защиту от reentrancy
* защиту от nested IRQ

Все перечисленное — ответственность системы выше.

---

## 6. Memory model assumptions

Предполагается:

* корректная реализация `std::atomic`
* корректная поддержка acquire/release
* cache-coherent архитектура
* циклическое переполнение `now` (modulo 2^32)

* Payload должен выполнять cache flush/invalidate
* Wrapper не добавляет memory barriers для DMA

---

## 7. Lifecycle methods (optional capabilities)

Wrapper поддерживает опциональные методы `Payload`:

* `init()`
* `alarm()`
* `done()`

Их наличие проверяется compile-time через `requires`.

Они:

* вызываются вне критического RT-пути
* не изменяют heartbeat semantics
* не влияют на memory ordering step()
* могут выполняться в non-RT контексте

---

## 8. System-level property

В системе:

```
Single scheduler + TaskWrapper + Payload
```

гарантируется:

> Deterministic single-entry execution with explicit completion publication.
> Все эффекты step() happen-before следующего чтения heartbeat.

При остановке scheduler:

* дальнейшие `step()` не выполняются
* heartbeat остаётся на последнем завершённом тике
* поведение полностью детерминировано

---

## 9. Misuse scenarios

Нарушение любого из следующих условий выводит компонент за пределы контракта:

* параллельный вызов `step()`
* reentrant вызов
* небезопасный доступ к Payload из другого контекста
* использование heartbeat как протокола синхронизации внутри одного тика
* вызов `step()` без `noexcept`

---

## 10. Design intent

`TaskWrapper` — это:

> Minimal RT linearization boundary.

Он предназначен для:

* формально проверяемого RT-ядра
* систем с явным разделением RT / non-RT
* time-triggered или fixed-priority scheduling

---
