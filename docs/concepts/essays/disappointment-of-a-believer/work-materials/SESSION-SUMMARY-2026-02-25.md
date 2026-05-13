# Session Summary — 2026-02-25

## Delivered documents

### SPMCSnapshot_v6.2 — RT Contract & Invariants.md
Финальная спецификация примитива. Ключевые решения:

**Модель памяти:**
- `K = N+2` слотов
- `published` — `atomic<uint8_t>`, скалярный индекс
- `busy_mask` — `atomic<uint32_t>`, консервативный индикатор для writer'а
- `refcnt[K]` — `atomic<uint8_t>`, точный счётчик читателей на слот
- `initialized` — `atomic<bool>`, сигнал первой публикации

**Инвариант W-NoOverwritePublished:** writer никогда не пишет в `published` слот. Из этого структурно следует отсутствие torn read — без verify, без retry.

**Теорема slot availability:** при K=N+2 writer всегда находит свободный не-published слот. Доказательство: N читателей занимают ≤N слотов → свободно ≥2 → максимум 1 из них published → остаётся ≥1.

**Порядок claim:**
- Установка: `busy_mask.fetch_or` ДО `refcnt.fetch_add` (false-positive безопасен, false-negative невозможен)
- Снятие: `refcnt.fetch_sub` ДО `busy_mask.fetch_and` (только при переходе 1→0)

**Theoretical Bounds:** теорема Херлихи 1991 — wait-free для обоих сторон на произвольном SMP недостижимо без примитива с consensus number ≥ 2.

**Условия корректности:**
- DA-A: одно ядро без вытеснения → строго wait-free
- DA-B: SMP с временным разделением → wait-free
- DA-C: произвольный SMP → reader lock-free с bounded CAS retry, writer wait-free

---

### SPMC-state-publishing-channel-task.md
Формальная постановка задачи. Требования:
- R1 No torn read, R2 No torn write, R3 Validity
- P1 Wait-free writer, P2 Wait-free reader (при платформенных условиях)
- **P3 RT-bounded execution** — корневое требование; отсутствие мьютексов/аллокаций/syscall — следствия
- **M1 Bounded memory** — фиксированная конечная память; N независимых каналов не решают torn read
- M2 No dynamic alloc (следствие P3)
- W1/W2 Deterministic O(1) WCET
- Секция impossibility theorem с доказательством

---

### Разочарование верующего.md
Статья для инженеров. Структура:
1. Что хочется — постановка с R1/R2/P1/P2/P3/M1
2. Предисловие — отвечает на "это очевидно, проходят на 3 курсе"; контекст диплома первой половины 90-х
3. Путь поиска — мьютекс (P3), SeqLock (P2), double buffer (torn read на SMP), N каналов (та же проблема × N)
4. Вроде нашли SPMCSnapshot — K=N+2, теорема, claim-протокол, шесть ревизий
5. Проверяем — три конкретные проблемы: refcnt для multi-reader слота, порядок claim, устранение NONE
6. Теоретическое ограничение — окно claim на SMP; disable_preemption не помогает (параллелизм ≠ вытеснение)
7. Теорема 1991 — иерархия консенсуса Херлихи, формальная недостижимость
8. Double/triple buffer облом — почему SPSCQueue работает (семантика очереди = эксклюзивное владение, нет claim), почему снапшот — нет
9. Отсутствие решения — три компромисса, честный итог

---

## Ключевые исправления в ходе сессии

| Версия | Проблема | Решение |
|---|---|---|
| v3→v4 | 1 бит не считает несколько читателей одного слота | Добавлен `refcnt[]` |
| v4.0→v4.1 | Порядок claim: refcnt до busy_mask → false-negative | Reversed: busy_mask ДО refcnt |
| v4.x→v6.0 | K=N+1 + verify → сложный протокол | K=N+2 + W-NoOverwritePublished, verify удалён |
| v6.0→v6.1 | Нет сигнала "до первой публикации" | Добавлен `initialized` atomic |
| v6.1→v6.2 | Нет анализа теоретических пределов | Добавлен раздел Theoretical Bounds |
| Статья | "double buffer работает для SPSC" | Исправлено: не работает на SMP |
| Статья | "N double buffer'ов работает" | Исправлено: та же дыра × N |
| P3 | "нет мьютексов" как требование | Переформулировано: RT-bounded execution (корень) |
| M1 | "минимум памяти" | Переформулировано: bounded memory; N каналов всё равно не решают проблему |

---

## Открытые вопросы для следующей сессии

1. **CAS-вариант псевдокода для DA-C** — lock-free reader для произвольного SMP не написан
2. **C++20 реализация SPMCSnapshot v6.2** — заголовочный файл для stam-rt-dto
3. **Интеграция в stam-rt-dto** — рядом с DoubleBuffer, TripleBuffer, SPSCRing, Mailbox2Slot, CRC32
4. **Финальная вычитка статьи** — особенно раздел про SeqLock (там тоже есть нюансы на SMP)
