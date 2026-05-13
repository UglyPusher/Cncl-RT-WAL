Ниже — документ уровня строгого инженерного анализа невозможности. Он продолжает `SPMC-state-publishing_channel-task.md`.

---

# SPMC State Publishing Channel — Impossibility Results

`docs/contracts/SPMC-state-publishing_channel-impossibility.md`
Revision 1.0 — February 2026

---

## 1. Назначение

Документ фиксирует **ограничения и невозможности** реализации канала:

SPMC state snapshot channel

при условиях:

* SMP
* fully preemptible readers
* wait-free read
* wait-free publish
* no torn read
* bounded memory

Этот документ показывает, какие классы решений **невозможны**, и почему.

---

## 2. Модель (из Task Definition)

Предполагается:

* один writer
* N readers
* SMP система
* произвольные interleavings
* readers могут быть вытеснены в любой момент
* writer может выполняться произвольно быстро
* память ограничена:

```
K = O(N)
```

Гарантии:

```
G1 No Torn Read
P1 Wait-Free Read
P2 Wait-Free Publish
```

---

## 3. Основная причина сложности

Любой reader должен:

1 определить slot i

```
i = published.load()
```

2 зафиксировать claim

```
busy[i] = 1
```

Между этими шагами существует окно гонки:

```
i = published.load()

<< arbitrary time >>

busy[i] = 1
```

В этом интервале writer может:

* изменить published
* выбрать slot i
* начать писать slot i

Если reader затем читает slot i:

```
reader read(slot[i])
writer write(slot[i])
```

возникает torn read.

Это фундаментальное окно гонки.

---

## 4. Невозможность Double Buffer

### Модель

```
slot A
slot B
published ∈ {A,B}
```

Writer:

```
write(not published)
published = new_slot
```

Reader:

```
i = published
read slot[i]
```

### Проблема

Reader может:

```
i = published(A)

<< preempted >>

writer publish B
writer publish A

reader read A
```

Writer может писать A одновременно с reader.

### Вывод

Double buffer не обеспечивает:

```
G1 No Torn Read
```

при SMP и preemption.

---

## 5. Невозможность Triple Buffer

### Модель

```
3 slots
```

Writer всегда пишет третий слот.

### Проблема

Reader может:

```
i = published(A)

<< preempted >>

writer publish B
writer publish C
writer publish A

reader read A
```

Writer может писать A одновременно.

### Вывод

Triple buffer не решает проблему.

---

## 6. Недостаточность K=N+1

Даже если:

```
reader holds ≤1 slot
K=N+1
```

Writer всегда имеет свободный slot.

Но reader может:

```
i = published

<< preempted >>

writer publish j
writer reuse i

reader claim i
reader read i
```

Busy не защищает.

---

## 7. Недостаточность Busy Mask

Busy mask:

```
busy[i] = reader holds i
```

Проблема:

busy устанавливается после выбора slot.

```
i = published

<< preempted >>

writer reuse i

busy[i]=1
```

Busy не защищает.

---

## 8. Недостаточность Non-Preemptible Window

Предположение:

```
published.load
busy.fetch_or
```

выполняются без вытеснения.

Это устраняет локальное окно.

Но SMP остаётся.

Writer может:

```
reader load published

writer publish j
writer publish i

reader busy[i]=1
reader read i
```

Reader читает slot i во время записи.

### Вывод

Non-preemptible window недостаточен.

---

## 9. Недостаточность Atomic OR Claim

Claim:

```
busy |= published
```

Это атомарная операция.

Но published читается отдельно.

```
p = published

<< arbitrary delay >>

busy |= p
```

Writer может reuse p.

---

## 10. Verify делает read не wait-free

Verify схема:

```
i = published
busy[i]=1

if published != i:
  busy[i]=0
  retry
```

Writer может менять published бесконечно.

Reader может retry бесконечно.

Следовательно:

```
P1 Wait-Free Read
```

нарушается.

---

## 11. Reservation делает publish не wait-free

Reservation схема:

```
reserve slot
if conflict:
  retry
```

Readers могут постоянно объявлять слоты.

Writer может retry бесконечно.

Следовательно:

```
P2 Wait-Free Publish
```

нарушается.

---

## 12. Основная теорема невозможности

При условиях:

* SMP
* fully preemptible readers
* arbitrary scheduling
* wait-free read
* wait-free publish
* no torn read
* bounded memory

невозможно реализовать SPMC state publishing channel
используя только:

```
published register
slot array
busy flags
refcounts
```

без дополнительных механизмов:

* verify
* announce/hazard
* epochs
* scheduler assumptions

---

## 13. Минимальные необходимые расширения

Хотя базовая задача невозможна, возможны решения если добавить:

### A. Scheduler assumptions

например:

```
bounded claim latency
```

или:

```
non-preemptible claim
```

---

### B. Hazard Announcement

```
announce[rid]
```

---

### C. Epoch/RCU

```
retire slots after grace period
```

---

### D. Verify + Relaxed Progress

```
read is lock-free
not wait-free
```

---

## 14. Инженерный вывод

SPMC state publishing channel с требованиями:

```
wait-free read
wait-free publish
no torn read
SMP
preemptible
bounded memory
```

является задачей с очень жёсткими ограничениями.

Простые конструкции:

* double buffer
* triple buffer
* busy mask
* refcount
* atomic OR claim

недостаточны.

---

## 15. Следующий шаг

Следующий документ:

```
SPMC-state-publishing_channel-design-space.md
```

где фиксируются:

* допустимые классы решений
* их tradeoffs
* требования к RT системе.
