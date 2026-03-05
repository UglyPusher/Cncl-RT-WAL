[Сессия 0000][Оператор]
Фаза 1.
Ваши соображения по модификации tags.md

[Сессия 0001][Клод]
Текущий tags.hpp содержит: rt_safe_tag, RtSafe, Steppable, RtPayload.
Зафиксированная документация требует четырёх изменений.

1. Добавить heartbeat_word_t и tick_t.
   Документ: "heartbeat_word_t — платформенный тип, равный ширине шины данных".
   На Cortex-M4 это uint32_t. tick_t = heartbeat_word_t (алиас, не отдельный тип).
   После объявления — static_assert на lock-free.

   Предлагаю:
       using heartbeat_word_t = uint32_t;
       using tick_t           = heartbeat_word_t;
       static_assert(std::atomic<heartbeat_word_t>::is_always_lock_free,
           "heartbeat_word_t must be lock-free atomic on this platform");

2. Исправить Steppable: заменить uint32_t now на tick_t now.
   Это следует из того, что tick_t — единственный тип времени в системе.

3. Добавить концепт RtHooks.
   Документ: "если опциональный хук объявлен, он должен быть noexcept".
   Концепт проверяет три хука (init/alarm/done): если метод есть — он noexcept.

   Предлагаю:
       template<class T>
       concept RtHooks =
           (!requires(T& t){ t.init();  } || requires(T& t){ { t.init()  } noexcept; }) &&
           (!requires(T& t){ t.alarm(); } || requires(T& t){ { t.alarm() } noexcept; }) &&
           (!requires(T& t){ t.done();  } || requires(T& t){ { t.done()  } noexcept; });

4. Расширить RtPayload: добавить RtHooks.
   Текущий: RtSafe<T> && Steppable<T>.
   Требуемый: RtSafe<T> && Steppable<T> && RtHooks<T>.

Порядок объявлений в файле после правки:
    heartbeat_word_t, tick_t, static_assert
    rt_safe_tag, rt_unsafe_tag
    RtSafe
    Steppable       ← использует tick_t
    RtHooks
    RtPayload       ← использует RtSafe + Steppable + RtHooks

Вопросов по tags.hpp у меня нет — изменения однозначно следуют из документации.
Жду подтверждения или замечаний перед переходом к обсуждению taskwrapper.hpp.

[Сессия 0002][Кодекс]
Подтверждаю направления по tags.hpp, в целом консенсус есть.

1. heartbeat_word_t/tick_t:
   поддерживаю `using heartbeat_word_t = uint32_t;` и `using tick_t = heartbeat_word_t;` для Cortex-M4.
   static_assert на lock-free нужен и уместен.

2. Steppable:
   параметр `now` должен быть `tick_t`, это убирает расхождения по типу системного времени.

3. RtHooks:
   логика "если метод есть, то он noexcept" корректная.
   Предлагаю проверять сигнатуры именно как в документации wrapper-а:
   - `init()`
   - `alarm(tick_t now)`
   - `done()`
   И для `alarm` проверять `noexcept` именно на версии с `tick_t`.

4. RtPayload:
   согласен расширить до `RtSafe<T> && Steppable<T> && RtHooks<T>`.

Уточнение по реализации:
   в `tags.hpp` сейчас объявлен только `rt_safe_tag`; если в wrapper используется `rt_unsafe_tag`, его нужно также объявить здесь (или согласованно убрать использование из wrapper). Это нужно проверить в фазе обсуждения taskwrapper.hpp, чтобы не осталось рассинхрона между концептами и враппером.

[Сессия 0003][Клод]
Ответ на сессию 0002.

По пп. 1, 2, 4 — полное согласие.

По п. 3 (сигнатура alarm):
Не согласен с `alarm(tick_t now)`.
Зафиксированная документация (STAM_task_wrapper_structure.md, раздел 3.3) явно указывает:
    void alarm() noexcept
Без параметров. Это не случайность — alarm вызывается safety-монитором,
который реагирует на факт нарушения таймаута, а не передаёт текущее время в payload.
Payload не должен принимать решения на основе `now` в alarm — это противоречило бы
разделению ответственности: scheduler/монитор управляет временем, payload реагирует на событие.
Предлагаю оставить `alarm()` без параметров строго по документации.

По уточнению про rt_unsafe_tag:
`rt_unsafe_tag` уже объявлен в текущем tags.hpp (строка 8: `struct rt_unsafe_tag {};`).
Он является парным маркером к `rt_safe_tag` — оба объявлены, ни один концепт его не использует
(RtSafe проверяет только rt_safe_tag). Рассинхрона нет. Правки не требуется.

---

[Консенсус] tags.hpp

Итог обсуждения (сессии 0000–0003). Все три участника согласны:

1. Добавить типы:
       using heartbeat_word_t = uint32_t;
       using tick_t           = heartbeat_word_t;
       static_assert(std::atomic<heartbeat_word_t>::is_always_lock_free,
           "heartbeat_word_t must be lock-free atomic on this platform");

2. Исправить Steppable:
       concept Steppable = requires(T& t, tick_t now) { { t.step(now) } noexcept -> same_as<void>; };

3. Добавить RtHooks (alarm без параметров):
       concept RtHooks =
           (!requires(T& t){ t.init();  } || requires(T& t){ { t.init()  } noexcept; }) &&
           (!requires(T& t){ t.alarm(); } || requires(T& t){ { t.alarm() } noexcept; }) &&
           (!requires(T& t){ t.done();  } || requires(T& t){ { t.done()  } noexcept; });

4. Расширить RtPayload:
       concept RtPayload = RtSafe<T> && Steppable<T> && RtHooks<T>;

5. Порядок объявлений в файле:
       heartbeat_word_t, tick_t, static_assert
       rt_safe_tag, rt_unsafe_tag
       RtSafe
       Steppable
       RtHooks
       RtPayload

6. rt_unsafe_tag — оставить как есть (companion-маркер, правок не требует).

Открытый вопрос (от Кодекса): alarm(tick_t now) vs alarm() — ЗАКРЫТ.
Решение: alarm() без параметров, строго по зафиксированной документации.

[Сессия 0004][Кодекс]
Проверил ответ и сверил с текущими файлами:
- `stam-rt-lib/include/model/tags.hpp`
- `stam-rt-lib/include/exec/tasks/taskwrapper.hpp`
- `docs/concepts/fixed/STAM_task_wrapper_structure.md` (разделы 3.1–3.3)

Подтверждаю: для `RtHooks` метод `alarm` должен оставаться без параметров (`alarm() noexcept`).
Моё замечание из 0002 про `alarm(tick_t now)` считаю закрытым.

[Консенсус]
1. Принимаем итог из блока `[Консенсус] tags.hpp` (сессия 0003) без изменений.
2. По `tags.hpp` консенсус достигнут, можно переходить к фазе обсуждения `taskwrapper.hpp`.

[Сессия 0005][Оператор]
Клод вносит изменения в tags.hpp. отчет дописавает в конверсейшн.

[Сессия 0006][Клод]
Изменения в tags.hpp внесены. Отчёт по каждому пункту консенсуса:

1. heartbeat_word_t / tick_t / static_assert — добавлены в начало namespace.
   Добавлен `#include <atomic>` (нужен для static_assert).

2. Steppable — параметр `now` заменён с `uint32_t` на `tick_t`.

3. RtHooks — концепт добавлен после Steppable.
   alarm() без параметров, строго по документации.

4. RtPayload — расширен до `RtSafe<T> && Steppable<T> && RtHooks<T>`.

5. Порядок объявлений в файле соответствует консенсусу:
   heartbeat_word_t → tick_t → static_assert → rt_safe_tag → rt_unsafe_tag →
   RtSafe → Steppable → RtHooks → RtPayload

6. rt_unsafe_tag — оставлен без изменений.

tags.hpp закрыт. Готов к обсуждению taskwrapper.hpp.

[Сессия 0007][Кодекс]
Проверил отчёт 0006 по фактическому файлу `stam-rt-lib/include/model/tags.hpp`.
Подтверждаю: все 6 пунктов консенсуса реализованы корректно и без расхождений.

Проверка по пунктам:
1. `#include <atomic>` добавлен, `heartbeat_word_t`, `tick_t` и `static_assert` присутствуют.
2. `Steppable` переведён на `tick_t now`.
3. `RtHooks` добавлен, `alarm()` проверяется без параметров.
4. `RtPayload` расширен до `RtSafe && Steppable && RtHooks`.
5. Порядок объявлений соответствует зафиксированному.
6. `rt_unsafe_tag` сохранён.

Итог: изменения по `tags.hpp` принимаю. Можно переходить к обсуждению/правкам `taskwrapper.hpp`.

[Сессия 0008][Оператор]

Переходим к врапперу. Ваши соображения.

[Сессия 0009][Клод]
Сравнил текущий taskwrapper.hpp с документацией (STAM_task_wrapper_structure.md).
Расхождений пять.

1. Конструктор принимает hb напрямую.
   Текущий:  explicit TaskWrapper(Payload& payload, std::atomic<uint32_t>& hb) noexcept
   Требуемый: explicit TaskWrapper(Payload& payload) noexcept
   Причина: hb привязывается registry.add() через attach_hb(), не разработчиком напрямую.

2. hb_ хранится как ссылка, должен быть указатель.
   Текущий:  std::atomic<uint32_t>& hb_
   Требуемый: std::atomic<heartbeat_word_t>* hb_ = nullptr
   Причина: двухфазная инициализация — в конструкторе hb_ == nullptr, после attach_hb() != nullptr.
   Ссылка не может быть nullptr и не позволяет двухфазную инициализацию.

3. Отсутствует метод attach_hb().
   Требуемый:
       void attach_hb(std::atomic<heartbeat_word_t>* hb) noexcept { hb_ = hb; }
   Вызывается внутренним кодом registry.add(). Не предназначен для прямого вызова разработчиком.

4. step() использует uint32_t вместо tick_t, и обращается к hb_ как к ссылке.
   Текущий:
       void step(uint32_t now) noexcept {
           payload_.step(now);
           hb_.store(now, std::memory_order_release);
       }
   Требуемый:
       void step(tick_t now) noexcept {
           assert(hb_ != nullptr);
           payload_.step(now);
           hb_->store(now, std::memory_order_release);
       }
   Причина: tick_t — единственный тип времени в системе.
   assert() проверяет, что attach_hb() был вызван до step(). В release-сборке assert вырезается.
   Нужен #include <cassert>.

5. Includes: <cstdint> не нужен (uint32_t теперь только через tags.hpp).
   Нужен #include <cassert>.
   Остальные includes корректны.

Изменений в init() / alarm() / done() не требуется — if constexpr уже корректен.
Ограничение шаблона `stam::model::Steppable Payload` — корректно, менять не нужно.

Итого по изменениям:
    - конструктор: убрать hb-параметр
    - hb_: ссылка → указатель с nullptr-инициализацией, тип uint32_t → heartbeat_word_t
    - добавить attach_hb()
    - step(): uint32_t → tick_t, hb_. → hb_->, добавить assert
    - includes: убрать <cstdint>, добавить <cassert>

Жду подтверждения или замечаний.

[Сессия 0010][Кодекс]
Проверил 0009: вывод корректный, все 5 расхождений подтверждаю по `STAM_task_wrapper_structure.md`.

Поддерживаю предложенный набор правок без изменений:
1. Конструктор только `TaskWrapper(Payload& payload) noexcept`.
2. `hb_` как `std::atomic<heartbeat_word_t>*` с инициализацией `nullptr`.
3. Добавить `attach_hb(std::atomic<heartbeat_word_t>* hb) noexcept`.
4. `step(tick_t now) noexcept` + `assert(hb_ != nullptr)` + запись `hb_->store(...)`.
5. По include: добавить `<cassert>`, убрать `<cstdint>`.

Доп. уточнение для реализации (рекомендую):
- в `attach_hb()` поставить debug-проверки `assert(hb != nullptr);` и `assert(hb_ == nullptr);`
  чтобы зафиксировать инвариант "привязка heartbeat ровно один раз".

Если возражений нет, считаю консенсус по изменению `taskwrapper.hpp` достигнут и можно переходить к фазе 2 (правки кода).

[Сессия 0011][Оператор]
Пока без возражений.

[Сессия 0012][Клод]
Принимаю дополнение Кодекса про asserts в attach_hb().
Оба assert обоснованы документацией (раздел 7):
    "attach_hb() вызывается внутренним кодом registry.add() ровно один раз. Повторный вызов — ошибка."
- assert(hb != nullptr)  — входящий указатель обязан быть валидным
- assert(hb_ == nullptr) — фиксирует инвариант "привязка ровно один раз"
В release-сборке оба assert вырезаются — zero-cost.

Возражений нет. Консенсус по taskwrapper.hpp достигнут.

---

[Консенсус] taskwrapper.hpp

Итог обсуждения (сессии 0008–0012). Все три участника согласны:

1. Конструктор — убрать hb-параметр:
       explicit TaskWrapper(Payload& payload) noexcept;

2. Поле hb_ — ссылка → указатель, nullptr-инициализация, тип heartbeat_word_t:
       std::atomic<heartbeat_word_t>* hb_ = nullptr;

3. Добавить attach_hb() с двумя debug-assert:
       void attach_hb(std::atomic<heartbeat_word_t>* hb) noexcept {
           assert(hb  != nullptr);   // реестр обязан передать валидный указатель
           assert(hb_ == nullptr);   // привязка ровно один раз
           hb_ = hb;
       }

4. step() — tick_t, assert перед записью, hb_->:
       void step(tick_t now) noexcept {
           assert(hb_ != nullptr);
           payload_.step(now);
           hb_->store(now, std::memory_order_release);
       }

5. init() / alarm() / done() — без изменений.

6. Ограничение шаблона stam::model::Steppable Payload — без изменений.

7. Includes:
       убрать: <cstdint>
       добавить: <cassert>


[Сессия 0013][Оператор]
Клод вносит изменения в taskwrapper.hpp. отчет дописавает в конверсейшн.

[Сессия 0014][Клод]
Изменения в taskwrapper.hpp внесены. Отчёт по каждому пункту консенсуса:

1. Конструктор — hb-параметр убран:
       explicit TaskWrapper(Payload& payload) noexcept

2. Поле hb_ — ссылка заменена на указатель с nullptr-инициализацией, тип heartbeat_word_t:
       std::atomic<heartbeat_word_t>* hb_ = nullptr;

3. attach_hb() добавлен с двумя debug-assert:
       assert(hb  != nullptr);
       assert(hb_ == nullptr);

4. step() — tick_t, assert(hb_ != nullptr), hb_->store:
       void step(tick_t now) noexcept

5. init() / alarm() / done() — без изменений.

6. Ограничение шаблона stam::model::Steppable Payload — без изменений.

7. Includes:
       убран: <cstdint>
       добавлен: <cassert>

Доп.: добавлены using-объявления в начало namespace для удобства:
       using stam::model::tick_t;
       using stam::model::heartbeat_word_t;

taskwrapper.hpp закрыт.

[Сессия 0015][Кодекс]
Проверил `conversation.md` и фактический `stam-rt-lib/include/exec/tasks/taskwrapper.hpp`.
Подтверждаю: отчёт 0014 корректен, все пункты консенсуса по wrapper реализованы без расхождений.

Проверка:
1. Конструктор без hb-параметра — выполнено.
2. `hb_` переведён на `std::atomic<heartbeat_word_t>*` с `= nullptr` — выполнено.
3. `attach_hb()` добавлен, оба debug-assert присутствуют — выполнено.
4. `step(tick_t now)` + `assert(hb_ != nullptr)` + `hb_->store(..., std::memory_order_release)` — выполнено.
5. `init()/alarm()/done()` без изменений — выполнено.
6. Ограничение шаблона `stam::model::Steppable` сохранено — выполнено.
7. include-правки (`<cassert>` добавлен, `<cstdint>` убран) — выполнено.

Примечание: добавленные `using stam::model::tick_t;` и `using stam::model::heartbeat_word_t;` не противоречат консенсусу и читаемость не ухудшают.
Итог: `taskwrapper.hpp` принимаю.

[Сессия 0016][Оператор]

Следующая тема - тесты для враппера.

Каталог stam-rt-lib\tests\exec\

Ваши соображения?

[Сессия 0017][Клод]
Изучил существующие тесты (primitives/tests/spmc_snapshot_test.cpp) и testing_guidelines.md.

Стиль проекта: кастомный harness без внешних зависимостей (TEST/RUN/EXPECT макросы, abort on fail).
Придерживаюсь этого же стиля.

Предлагаю один файл: stam-rt-lib/tests/exec/taskwrapper_test.cpp
Три группы тестов.

--- Группа 1: Compile-time (static_assert) ---
Проверяются концепты на синтетических payload-типах.
Не требуют RUN — проверяются самим компилятором.

    // MinimalPayload: только step() noexcept → Steppable, не RtPayload
    static_assert( Steppable<MinimalPayload>);
    static_assert(!RtPayload<MinimalPayload>);   // нет rt_safe_tag

    // FullPayload: step + rt_safe_tag + init/alarm/done noexcept → RtPayload
    static_assert(RtPayload<FullPayload>);

    // HookNotNoexcept: alarm() без noexcept → не RtHooks
    static_assert(!RtHooks<HookNotNoexcept>);

    // NoRtTag: step есть, rt_safe_tag нет → не RtSafe
    static_assert(!RtSafe<NoRtTag>);

--- Группа 2: Unit tests ---
Каждый тест создаёт payload-mock, atomic hb, wrapper; вызывает attach_hb, затем метод.

    step_calls_payload_step_with_correct_now
        — payload.step вызван ровно 1 раз с переданным now

    step_updates_heartbeat_to_now
        — hb.load() == now после step

    step_order_payload_before_hb
        — payload.step выполнен ДО hb->store
        — реализация: payload.step записывает last_step_tick; assert last_step_tick перед hb

    init_called_if_present
        — payload с init(); вызываем wrapper.init(); счётчик init == 1

    init_not_called_if_absent
        — payload без init(); wrapper.init() не ломается, ничего не вызывает

    alarm_called_if_present
    alarm_not_called_if_absent

    done_called_if_present
    done_not_called_if_absent

--- Группа 3: Contract tests (инварианты из документации) ---

    heartbeat_updated_on_every_step
        — 3 последовательных step(1), step(2), step(3); hb == 1, 2, 3 соответственно

    attach_hb_sets_heartbeat_pointer
        — до attach_hb: hb_ == nullptr (проверяем косвенно — step не вызываем)
        — после attach_hb(ptr): step(now) обновляет *ptr

    step_does_not_call_init_alarm_done
        — payload с init/alarm/done; вызываем только step; счётчики init/alarm/done == 0

--- Что намеренно не тестируется ---

    double attach_hb — вызывает assert(hb_ == nullptr) → abort в debug-build.
    Тест через death-test не входит в стиль проекта (нет gtest).
    Достаточно coverage на уровне code review + документация инварианта.

    step без attach_hb — assert(hb_ != nullptr) → abort в debug-build.
    Аналогично.

--- Структура файлов ---

    stam-rt-lib/
      tests/
        exec/
          taskwrapper_test.cpp   ← единственный новый файл
        CMakeLists.txt           ← нужно добавить exec/ subdirectory

Вопрос к участникам: нужен ли отдельный test_payload.hpp с mock-типами,
или inline-struct прямо в .cpp (как в spmc_snapshot_test)?
В существующих тестах — inline, предлагаю тот же подход.

[Сессия 0018][Кодекс]
Проверил предложение 0017 и текущий `taskwrapper.hpp` после правок.
В целом подход верный, поддерживаю структуру из 3 групп. Есть 2 уточнения для корректности тестов.

1) `step_order_payload_before_hb`:
   в текущем API wrapper нет точки наблюдения "момент store", поэтому порядок "до/после" из payload проверить напрямую нельзя без инструментирования wrapper-а.
   Предлагаю заменить на проверяемый контракт:
   - `step_invokes_payload_and_then_updates_observable_state` через payload-счётчик + итоговый `hb == now`.
   Это проверяет функциональный эффект без хрупких предположений о внутренней реализации.

2) `attach_hb_sets_heartbeat_pointer`:
   формулировку "до attach_hb: hb_ == nullptr" оставить как документируемый инвариант, но в тесте проверять только наблюдаемое поведение:
   - после `attach_hb(&hb)` вызов `step(now)` обновляет именно этот `hb`.
   Прямой доступ к `hb_` недоступен и не нужен.

По остальному согласен:
- compile-time `static_assert` на `Steppable/RtSafe/RtHooks/RtPayload` — обязательно;
- unit на `init/alarm/done` (present/absent) — корректно;
- `heartbeat_updated_on_every_step` — корректно;
- death-тесты для assert-веток (`double attach_hb`, `step без attach_hb`) можно не делать в рамках текущего test harness.

Ответ на вопрос по структурам mock:
использовать inline-struct в `taskwrapper_test.cpp` (в стиле существующих тестов), отдельный `test_payload.hpp` не нужен.

[Сессия 0019][Клод]
Согласен с обоими уточнениями Кодекса.

По п.1 (step_order):
Принимаю замену. Прямой проверки порядка payload→hb нет без инструментирования самого враппера,
что выходит за рамки black-box теста. Тест `step_invokes_payload_and_then_updates_observable_state`
проверяет то, что реально наблюдаемо: payload вызван (счётчик) + hb обновлён до ожидаемого значения.
Этого достаточно — порядок внутри step() гарантирован кодом, не тестом.

По п.2 (attach_hb):
Принимаю. hb_ — приватное поле, прямой доступ не нужен и нежелателен.
Тест через наблюдаемое поведение (step обновляет именно тот hb, который передали) — корректен и достаточен.

Консенсус достигнут.

---

[Консенсус] taskwrapper_test.cpp

Итог обсуждения (сессии 0016–0019). Все три участника согласны:

Файл: stam-rt-lib/tests/exec/taskwrapper_test.cpp
Стиль: inline mock-структуры в .cpp, кастомный harness (TEST/RUN/EXPECT), без внешних зависимостей.

--- Группа 1: Compile-time (static_assert, не требуют RUN) ---

    static_assert( Steppable<MinimalPayload>)   // только step() noexcept
    static_assert(!RtPayload<MinimalPayload>)    // нет rt_safe_tag
    static_assert( RtPayload<FullPayload>)       // step + rt_safe_tag + хуки noexcept
    static_assert(!RtHooks<HookNotNoexcept>)     // хук без noexcept
    static_assert(!RtSafe<NoRtTag>)              // нет rt_safe_tag

--- Группа 2: Unit tests (9 тестов) ---

    step_calls_payload_step_with_correct_now
    step_updates_heartbeat_to_now
    step_invokes_payload_and_updates_observable_state  ← заменяет step_order
    init_called_if_present
    init_not_called_if_absent
    alarm_called_if_present
    alarm_not_called_if_absent
    done_called_if_present
    done_not_called_if_absent

--- Группа 3: Contract tests (3 теста) ---

    heartbeat_updated_on_every_step
        — step(1), step(2), step(3); hb == 1, 2, 3 после каждого

    attach_hb_step_updates_attached_heartbeat
        — после attach_hb(&hb): step(now) обновляет именно &hb  ← уточнено

    step_does_not_call_init_alarm_done
        — payload с init/alarm/done; только step(); счётчики хуков == 0

--- Намеренно не тестируется ---

    double attach_hb   → assert(hb_ == nullptr) → abort; нет death-test в harness
    step без attach_hb → assert(hb_ != nullptr) → abort; аналогично

--- Структура файлов ---

    stam-rt-lib/
      tests/
        exec/
          taskwrapper_test.cpp   ← новый файл
        CMakeLists.txt           ← добавить exec/ subdirectory (или создать новый)


[Сессия 0020][Клод]
Тесты написаны, сборочные файлы добавлены.

Созданные файлы:
    stam-rt-lib/tests/exec/taskwrapper_test.cpp   — тесты
    stam-rt-lib/tests/exec/main.cpp               — точка входа
    stam-rt-lib/tests/exec/CMakeLists.txt         — сборка

Изменённые файлы:
    stam-rt-lib/CMakeLists.txt  — добавлен option(BUILD_EXEC_TESTS ON) и add_subdirectory(tests/exec)

Отчёт по группам.

Группа 1: Compile-time (5 static_assert).
Проверяют концепты на 4 синтетических типах. Исполнения не требуют — ошибка = ошибка компиляции.
    static_assert( Steppable<MinimalPayload>)
    static_assert(!RtPayload<MinimalPayload>)
    static_assert( RtPayload<FullPayload>)
    static_assert(!RtHooks<HookNotNoexcept>)
    static_assert(!RtSafe<NoRtTag>)

Группа 2: Unit tests (9 тестов).
Используют SpyPayload (счётчики + last_step_now) и NoHooksPayload (только step).
    step_calls_payload_step_with_correct_now
    step_updates_heartbeat_to_now
    step_invokes_payload_and_updates_observable_state
    init_called_if_present
    init_not_called_if_absent
    alarm_called_if_present
    alarm_not_called_if_absent
    done_called_if_present
    done_not_called_if_absent

Группа 3: Contract tests (3 теста).
    heartbeat_updated_on_every_step         — step(1/2/3), hb == 1/2/3 каждый раз
    attach_hb_step_updates_attached_heartbeat — step обновляет именно &hb из attach_hb
    step_does_not_call_init_alarm_done      — счётчики хуков == 0 после двух step

Harness: кастомный (TEST/RUN/EXPECT), abort on fail, без внешних зависимостей.
Запуск: cmake --build ... && ./stam_exec_tests  (или ctest -R stam_exec_tests)

[Сессия 0021][Кодекс]
Проверил ответ 0020 и фактические файлы тестов/сборки:
- `stam-rt-lib/tests/exec/taskwrapper_test.cpp`
- `stam-rt-lib/tests/exec/main.cpp`
- `stam-rt-lib/tests/exec/CMakeLists.txt`
- `stam-rt-lib/CMakeLists.txt`

Итог по тестам:
1. Состав тестов соответствует зафиксированному консенсусу (5 compile-time static_assert + 12 runtime тестов).
2. Локальный запуск `stam_exec_tests` успешен: 12/12 PASS.

Важное замечание по интеграции:
3. Полный build дерева сейчас падает в `apps/demo/trivial_tasks/main.cpp`:
   там осталось старое использование конструктора `TaskWrapper(payload, hb)`,
   тогда как после согласованных правок wrapper принимает только `TaskWrapper(payload)` и hb привязывается через `attach_hb()`.

Вывод:
- ответ 0020 по самим тестам подтверждаю;
- дополнительно фиксирую регресс совместимости в demo-app, который нужно отдельно поправить, чтобы full build снова проходил.

[Сессия 0022][Оператор]

Клод исправляет apps/demo/trivial_tasks/main.cpp Отчет сюда.

[Сессия 0023][Клод]
Файл apps/demo/trivial_tasks/main.cpp исправлен. Отчёт.

Проблема: CTAD (вывод аргументов шаблона) не работает с constrained templates
в ряде компиляторов и clangd при одном аргументе конструктора.
Старый код использовал двухаргументный конструктор, где вывод шёл по двум параметрам.

Решение: явное указание типа Payload.

До:
    exec::tasks::TaskWrapper w_rt{rt};
    exec::tasks::TaskWrapper w_nrt{nrt};

После:
    exec::tasks::TaskWrapper<modules::demo::trivial_rt_task>    w_rt{rt};
    exec::tasks::TaskWrapper<modules::demo::trivial_nonrt_task> w_nrt{nrt};

attach_hb / step — без изменений.
hb_rt / hb_nrt остались std::atomic<uint32_t> — совместимо, т.к. heartbeat_word_t = uint32_t.

Диагностика IDE после правки: ошибок нет.

[Сессия 0024][Кодекс]
Проверил ответ 0023 и фактический `apps/demo/trivial_tasks/main.cpp`.
Подтверждаю: правка корректная, сборка снова проходит.

Проверка:
1. В `main.cpp` действительно применено явное указание шаблонного параметра:
   - `TaskWrapper<modules::demo::trivial_rt_task> w_rt{rt};`
   - `TaskWrapper<modules::demo::trivial_nonrt_task> w_nrt{nrt};`
2. `attach_hb()` используется корректно для обоих wrapper-ов.
3. Повторная сборка дерева (`cmake --build /tmp/build`) успешна, включая `app_trv_task`.
4. Рантайм-проверка `stam_exec_tests` успешна: 12/12 PASS.

Уточнение:
- `ctest --test-dir /tmp/build -R stam_exec_tests` сейчас возвращает "No tests were found".
  Это не блокирует задачу (бинарник тестов запускается и проходит), но для запуска через ctest из корня
  потребуется отдельная проверка/настройка регистрации тестов на уровне top-level CMake.

[Сессия 0025][Опреатор]

Кодекс проводит отдельную проверку/настройку регистрации тестов на уровне top-level CMake.
Отчет сюда

[Сессия 0026][Кодекс]
Отдельную проверку/настройку регистрации тестов на уровне top-level CMake выполнил.

Причина проблемы:
- в `/src/CMakeLists.txt` `enable_testing()` вызывался слишком поздно (после `add_subdirectory(stam-rt-lib)`).
- из-за этого `ctest` из корня не видел тесты `stam-rt-lib/tests/exec`, хотя бинарник собирался и запускался вручную.

Исправление:
- перенёс вызов `enable_testing()` вверх, сразу после объявления `option(BUILD_TESTS ...)`,
  до добавления подпроектов.
- блок внизу оставил только для подключения `add_subdirectory(tests)` при `BUILD_TESTS=ON`.

Проверка после правки:
1. `ctest --test-dir /tmp/build -N` теперь видит тесты:
   - `stam_primitives_tests`
   - `stam_exec_tests`
2. `cmake --build /tmp/build && ctest --test-dir /tmp/build -R stam_exec_tests` — PASS.

Итог: регистрация `stam_exec_tests` через top-level `ctest` восстановлена.

[Сессия 0025][Опреатор]

Клод проверяет консистентность всей кодовой базы. Отчет сюда.

[Сессия 0027][Клод]
Проверка консистентности выполнена. Все файлы согласованы с зафиксированной документацией.

**1. model/tags.hpp** — синхронизирован полностью.
- `heartbeat_word_t = uint32_t`, `tick_t = heartbeat_word_t` ✓
- `static_assert` lock-free после объявления ✓
- `Steppable` использует `tick_t` ✓
- `RtHooks`: три хука, if-present-then-noexcept ✓
- `RtPayload = RtSafe && Steppable && RtHooks` ✓

**2. exec/tasks/taskwrapper.hpp** — синхронизирован полностью.
- Конструктор принимает только `payload`, `hb_ = nullptr` по умолчанию ✓
- `attach_hb()` с двумя assert'ами (не nullptr, однократная привязка) ✓
- `step(tick_t)`: assert(hb_), вызов payload, store с `memory_order_release` ✓
- Хуки через `if constexpr` ✓
- Copy/move deleted ✓

**3. apps/demo/trivial_tasks/main.cpp** — синхронизирован.
- Явные шаблонные параметры `TaskWrapper<trivial_rt_task>` и `TaskWrapper<trivial_nonrt_task>` ✓
- `attach_hb()` вызван раздельно для обоих wrapper'ов ✓
- `hb_rt`, `hb_nrt` остаются `std::atomic<uint32_t>` — совместимо, т.к. `tick_t == uint32_t` ✓

**4. stam-rt-lib/tests/exec/** — корректен.
- 5 static_assert + 9 unit + 3 contract = 12 тестов, соответствует консенсусу сессий 0016–0019 ✓
- `main.cpp` вызывает `taskwrapper_tests()` ✓
- `CMakeLists.txt`: цель `stam_exec_tests`, линковка к `stam_exec`, `add_test` ✓

**5. CMake-инфраструктура** — консистентна после правки Кодекса (сессия 0026).
- Корневой `CMakeLists.txt`: `enable_testing()` до `add_subdirectory(stam-rt-lib)` ✓
- `stam-rt-lib/CMakeLists.txt`: опция `BUILD_EXEC_TESTS ON`, подключает `tests/exec` ✓
- `BUILD_EXEC_TESTS` независим от корневого `BUILD_TESTS` — по дизайну, т.к. тесты exec-уровня не зависят от глобального флага

**Замечания:**
- `trivial_rt_task` имеет `using rt_class = rt_safe_tag` → удовлетворяет `RtPayload` ✓
- `trivial_nonrt_task` имеет `using rt_class = rt_unsafe_tag` → удовлетворяет только `Steppable`, не `RtPayload` — корректно для non-RT задачи ✓
- Модули задают `step(uint32_t)`: так как `tick_t = uint32_t`, концепт `Steppable` выполняется ✓

**Итог:** расхождений между кодом, тестами и зафиксированной документацией не обнаружено. Кодовая база согласована.


[Сессия 0028][Оператор]

Проверяем каталог примитивов. Задача Привести все примитивы к корректной работе в СМП окружении. Ваши соображения???

[Сессия 0029][Клод]
Прочитал все четыре примитива. Картина следующая.

---

**SPSCRing — уже SMP-корректен. Изменений не требует.**

Использует `memory_order_release/acquire` на `head_` и `tail_` без `preemption_disable`.
Это классический lock-free SPSC ring — доказанно корректен на SMP.

---

**DoubleBuffer — SMP-некорректен. Протоколная проблема.**

Суть: читатель загружает `published` (acquire), затем копирует `buffers[idx].value`.
На SMP писатель на другом ядре может сделать ДВА цикла записи пока читатель копирует:
первый цикл меняет published на `idx^1`, второй — обратно на `idx`, и начинает
записывать в `buffers[idx]` пока читатель ещё копирует оттуда. Torn read.

Код это честно документирует в SAFETY NOTE.

Предлагаемое исправление: **SeqLock**.
Писатель инкрементирует `seq` перед записью и после.
Читатель: `seq1 = load(seq)`, если нечётное — повторить; copy; `seq2 = load(seq)`;
если `seq1 != seq2` — повторить. SeqLock стандартен для SPSC-snapshot на SMP.
Минус: retry-loop у читателя; wait-free гарантия теряется, O(1) в среднем.

---

**Mailbox2Slot — SMP-некорректен. Фундаментальная гонка.**

Суть: `preemption_disable` блокирует только своё ядро.
Писатель и читатель бегут на разных ядрах → их критические секции не взаимно исключают друг друга.

Конкретный сценарий гонки:
- Состояние: `pub_state = 1`, `lock_state = kUnlocked`
- Писатель (ядро 1): загружает `lock_state = kUnlocked` → `j = 1`
  (потому что `locked != kSlot1` → `j = kSlot1 = 1`)
  → `pub_state == j = 1` → инвалидирует: `pub_state = kNone` (release)
- Читатель (ядро 0): загружает `p1 = pub_state` — если это произошло ДО инвалидации → `p1 = 1`
  → `lock_state.store(1)` — но писатель уже принял решение в пользу `j=1`
  → `p2 = pub_state` — если это произошло ДО инвалидации → `p2 = 1 = p1`
  → читатель считает чтение консистентным
- Писатель продолжает: записывает `slots[1].value` одновременно с копированием читателя

Проблема: `lock_state.store` читателя и `lock_state.load` писателя не синхронизированы —
на SMP нет гарантии что писатель увидит `lock_state = 1` до своего решения.

Исправление сложнее, чем просто заменить `preemption_disable`.
Протокол claim-verify требует, чтобы claim читателя был виден писателю ДО того,
как писатель выберет слот. На SMP это требует CAS: читатель атомарно:
`CAS(lock_state, kUnlocked, p1)` с проверкой что `pub_state` не изменился.
Либо полноценный spinlock обёртывающий выбор слота обеими сторонами.

---

**SPMCSnapshot — SMP-некорректен. Аналогичное окно.**

Окно между шагами 2 и 3 у читателя:
- Шаг 2: `load(published) → i`
- Шаг 3: `busy_mask |= (1 << i)`

На SMP писатель может выполнить ДВА цикла publish() пока читатель ещё не выставил бит.
В ходе второго цикла писатель видит `busy_mask` без бита `i`, и может выбрать `j = i`
(если `i != published` на тот момент). Результат: писатель пишет в слот `i`,
читатель читает из слота `i` — torn read.

Спецификация явно указывает на это как "CAS-model alternative".
Исправление: в читателе атомарно выполнить `if (published == i) then set busy_mask[i]`,
т.е. `compare_exchange` на `busy_mask` с верификацией `published` в одной операции.
Либо: установить `busy_mask` первым, затем проверить `published` не изменился (load-fence-verify).

---

**Итог по четырём примитивам:**

| Примитив | SMP-статус | Механизм проблемы | Направление исправления |
|---|---|---|---|
| SPSCRing | ✓ корректен | — | — |
| DoubleBuffer | ✗ torn read | 2 цикла писателя при копировании | SeqLock |
| Mailbox2Slot | ✗ stale lock_state | preemption ≠ mutual exclusion на SMP | CAS-based claim |
| SPMCSnapshot | ✗ busy_mask race | load→set не атомарно относительно писателя | CAS verify published |

Прежде чем двигаться к реализации — жду мнения Кодекса,
в частности по Mailbox2Slot: CAS-подход ломает wait-free на обеих сторонах.
Возможно, правильнее объявить Mailbox2Slot "uniprocessor only" и сделать
отдельный SMP-вариант с другим протоколом.

[Сессия 0030][Кодекс]
Проверил разбор 0029. В целом согласен с классификацией и рисками.

Что подтверждаю:
1. `SPSCRing` оставляем как есть (SMP-корректный SPSC протокол на acquire/release).
2. `DoubleBuffer` в текущем виде не даёт строгой SMP-гарантии консистентного снимка при копировании payload.
3. `Mailbox2Slot` и `SPMCSnapshot` имеют критичные окна между чтением published/lock и публикацией claim (busy/lock), что на SMP может дать запись в читаемый слот.

Предложение по стратегии спринта:
1. Зафиксировать матрицу гарантий в docs: `UP-safe` vs `SMP-safe` для каждого примитива.
2. Не менять "тихо" семантику существующих типов, а ввести SMP-варианты отдельно:
   - `DoubleBufferSeqLock` (или отдельный SMP-режим с явной документацией retry-semantics),
   - `Mailbox2SlotSmp` (CAS/lock-based claim protocol),
   - `SPMCSnapshotSmp` (CAS-verify claim на слот).
3. Для текущих реализаций (если остаются legacy) явно пометить ограничения `single-core / preemptive UP only` в заголовках и fixed-доках.
4. Для каждого SMP-варианта сразу добавить стресс-тесты с форсированными interleaving (многопоточный раннер, большие итерации, проверка torn/ABA симптомов).

По `Mailbox2Slot` поддерживаю вывод Клода: лучше отдельный SMP-вариант, чем ломать текущий контракт и латентность legacy-типа.

[Сессия 0031][Оператор]

Предложение кодекса нравится. Если нет супертяжелых стопперов. Поддерживаю. Клод, твое мнение? Добавь раздел консенсус.

[Сессия 0032][Клод]
Поддерживаю стратегию Кодекса. Стопперов нет. Одно уточнение по реализации.

**Уточнение по DoubleBufferSeqLock — именование:**
Имя `DoubleBufferSeqLock` предлагаю принять как итоговое: оно раскрывает
retry-семантику читателя (не wait-free, но O(1) в среднем при ограниченной частоте писателя).
Это важно для RT-пользователя: он должен видеть из имени, что у читателя есть петля повтора.

**Уточнение по SPMCSnapshotSmp — корректный SMP-fix:**
Вариант "set-then-verify" (сначала выставить `busy_mask[i]`, затем проверить `published == i`)
НЕ закрывает гонку полностью: писатель мог загрузить `busy_mask` ДО выставления бита,
выбрать `j = i` и писать в `slots[i]` одновременно с читателем.

Однако CAS-loop здесь НЕ нужен. Корректный wait-free SMP-fix — протокол в одну попытку:

    i = load(published, acq)
    busy_mask.fetch_or(1<<i, acq_rel)   // выставить claim
    i2 = load(published, acq)           // re-verify
    if i2 != i: busy_mask.fetch_and(~(1<<i), rel); fail
    // copy slots[i] — safe

Почему это корректно:
Если `published == i` в re-verify: с момента нашего `fetch_or` бит `i` стоит в `busy_mask`.
Любой будущий цикл писателя загружает `busy_mask` с битом `i` и не может выбрать `j = i`.
Если писатель ЗА ДО fetch_or загрузил stale busy (без бита `i`) — он тогда видел
`published == i` и обязан исключить `i` из candidates через `~(1<<pub)`. Конфликта нет.
Если `published != i` в re-verify: читатель снимает claim и выходит — копирования нет.

Это wait-free (одна попытка, без loop), O(1) — RT-совместимо.

**По остальным пунктам стратегии Кодекса — без возражений.**

---

[Консенсус]

1. SPSCRing — не трогаем. Уже SMP-корректен.

2. DoubleBuffer → DoubleBufferSeqLock (SMP-вариант).
   Протокол: писатель обёртывает запись seq-счётчиком (чётный→нечётный→чётный).
   Читатель: seq1 = load; copy; seq2 = load; если seq1 != seq2 или нечётный — повторить.
   Retry-семантика явна из имени. Существующий DoubleBuffer: маркировать UP-only.

3. Mailbox2Slot → Mailbox2SlotSmp (SMP-вариант).
   Протокол: детали при реализации; preemption_disable заменяется аппаратно-видимой синхронизацией.
   Существующий Mailbox2Slot: маркировать UP-only.

4. SPMCSnapshot → SPMCSnapshotSmp (SMP-вариант).
   Протокол читателя: fetch_or(1<<i) + re-verify(published == i) + при несовпадении fetch_and(~(1<<i)).
   Wait-free, O(1). Существующий SPMCSnapshot: маркировать UP-only.

5. Документация: добавить матрицу UP-safe / SMP-safe в primitives_README.md.

6. Тесты: для каждого SMP-варианта — стресс-тест с потоками, большим числом итераций,
   проверкой на torn-read и ABA симптомы.

[Сессия 0033][Кодекс]
Проверил 0032. Поддерживаю консенсус в текущем виде.

Подтверждаю ключевые моменты:
1. Разделение на legacy (UP-only) и новые SMP-варианты — правильное решение по совместимости.
2. `DoubleBufferSeqLock` — корректное и прозрачное имя (retry-семантика читается сразу).
3. Для `SPMCSnapshotSmp` принимаю предложенный протокол `fetch_or + re-verify + optional fetch_and` как базовый wait-free путь.

Практический порядок следующего спринта (чтобы снизить риск):
1. Сначала обновить docs/матрицу гарантий и пометки UP-only в текущих примитивах.
2. Затем реализовать `DoubleBufferSeqLock` и `SPMCSnapshotSmp` + стресс-тесты.
3. После этого реализовать `Mailbox2SlotSmp` (самый рискованный протокол) + отдельный набор стресс-тестов/инвариантов.

Если возражений нет, предлагаю в следующей сессии зафиксировать технические контракты (API + memory order) по каждому новому SMP-типу перед кодом.


[Сессия 0034][Оператор]

Мне нравится направление разработки СМП-примитивов как отдельного набора.
Клод?

[Сессия 0035][Клод]
Поддерживаю. Отдельный набор — правильное архитектурное решение.

Поддерживаю и предложение Кодекса: зафиксировать технические контракты
(API + порядки памяти) до кода. Это то, что мы сделали с TaskWrapper —
и именно наличие зафиксированной документации позволило пройти спринт
без споров о семантике в процессе реализации.

**Моё предложение по организации работы следующего спринта:**

Фаза 1. Маркировка UP-only (быстрая) — внести в каждый существующий заголовок
явную пометку `// UP-only: not safe on SMP` в блок CONTRACT и обновить
`primitives_README.md` с матрицей гарантий.

Фаза 2. Контракты SMP-примитивов — отдельный документ (по образцу
`STAM_task_wrapper_structure.md`) для каждого из трёх новых типов.
Минимум: API, топология (1P/NC), гарантии памяти, retry-семантика (если есть), ограничения.

Фаза 3. Реализация в порядке Кодекса: DoubleBufferSeqLock → SPMCSnapshotSmp → Mailbox2SlotSmp.

**По Mailbox2SlotSmp отдельно:**
Это самый нетривиальный случай. На SMP протокол claim-verify нуждается в
аппаратно-видимой синхронизации между двумя сторонами. Есть два кандидата:

Вариант A — spinlock (mutex на acquire/release) вокруг обеих критических секций.
Простой, корректный, но вводит взаимное ожидание: если писатель держит lock,
читатель спинит, и наоборот.

Вариант B — переработанный протокол на seq-lock-like basis:
писатель публикует с seq-счётчиком, читатель читает seq1 → загружает слот → читает seq2;
если изменился — miss. Аналог DoubleBufferSeqLock, но с семантикой latest-wins и false=miss.

Оба варианта ломают wait-free. Предлагаю обсудить выбор между A и B в начале фазы 2
(контракты), а не в процессе реализации.

[Сессия 0036][Оператор]

Мьютекс - точно отказать. 

Оба варианта ломают wait-free. - Плевать! На СМП доступны только лок фри гарантии читателя. Писатель остается вейт-фри.

Сначала правим контракты и инварианты для каждого нового примитива, потом создаем новые примитивы. Потом унифицируем интерфейсы ВСЕХ примитивов для наименьшего оверхеда при разработке и связывании каналов.

[Сессия 0037][Кодекс]
Принял ограничения:
1. spinlock/mutex в `Mailbox2SlotSmp` — исключаем.
2. Для SMP-версий целевая модель: writer wait-free, reader lock-free (достаточно).
3. Порядок работ: контракты/инварианты → реализация новых SMP-примитивов → унификация интерфейсов всех примитивов.

Поддерживаю такой порядок.

Чтобы сразу перейти к фазе контрактов, предлагаю зафиксировать единый шаблон для всех трёх новых типов:
1. Topology: число producer/consumer, допущения по потокам/ядрам.
2. Progress guarantees: отдельно для writer и reader (`wait-free` / `lock-free` / `blocking`).
3. API surface: `publish/try_read` (или эквивалент), сигнатуры и return-semantics.
4. Memory model: точные `memory_order` по каждой атомарной операции.
5. Invariants: запрещённые состояния и условия безопасности (нет torn-read, нет write-into-claimed-slot).
6. Failure/retry semantics: когда reader возвращает miss/retry и что это означает для клиента.
7. Cost model: O(1), число атомарных RMW на fast path.

Если согласны, в следующей сессии подготовлю черновик таких контрактов для:
- `DoubleBufferSeqLock`
- `SPMCSnapshotSmp`
- `Mailbox2SlotSmp`

[Сессия 0038][Клод]
Принимаю ограничения оператора. Шаблон контрактов от Кодекса — хороший, принимаю без изменений.

Одно техническое наблюдение перед тем как Кодекс пишет черновики.

**По SPMCSnapshotSmp — читатель фактически wait-free:**
Протокол `fetch_or(1<<i) + re-verify + optional fetch_and` делает одну попытку per tick.
Нет петли повтора. С точки зрения прогресс-гарантий читатель — wait-free per invocation,
не только lock-free. Стоит зафиксировать это в контракте точно.

**По Mailbox2SlotSmp — предлагаю пересмотреть дизайн:**

Оригинальный Mailbox2Slot построен на двухслотовом claim-verify протоколе.
Адаптировать его для SMP сложно: claim-verify требует аппаратно-видимой синхронизации
между двумя сторонами без spinlock — то есть CAS-цепочку, которая либо усложняет
протокол, либо делает одну из сторон не wait-free.

Есть более простая альтернатива для Mailbox2SlotSmp на SeqLock-основе:

    Один слот (не два):
    Писатель: seq.fetch_add(1, release) → write slot → seq.fetch_add(1, release)
    Читатель (одна попытка):
        seq1 = load(seq, acquire)
        if seq1 % 2 != 0: return false   // писатель в процессе
        copy slot
        seq2 = load(seq, acquire)
        if seq1 != seq2: return false    // запись была во время копирования
        return true

    Писатель: wait-free, O(1).
    Читатель: wait-free per invocation (одна попытка → false или true), O(1).
    SMP: корректен — seq-верификация поймает любой concurrent write.

Семантика идентична оригинальному Mailbox2Slot:
latest-wins, false при гонке (sticky-state у клиента), false до первой публикации.

Преимущество: 1 слот вместо 2, нет claim-verify, нет preemption_disable.
Недостаток: при больших T читатель чаще получает false (писатель пишет дольше).
Для RT-систем с небольшими T (структуры данных датчиков, команды) это приемлемо.

Предлагаю Кодексу рассмотреть этот вариант при написании контракта Mailbox2SlotSmp.
Если двухслотовый подход принципиален — нужно обоснование зачем он лучше SeqLock на SMP.


[Сессия 0039][Оператор]

Может тогда переводить Mailbox2Slot в 3-х слотовый вариант?? Но тогда это просто вырожденный вариант СПМЦ.

Кодекс?

[Сессия 0040][Кодекс]
Хороший вопрос. Коротко: для `Mailbox2SlotSmp` 3 слота не обязательны и, как базовое решение, не выгоднее seq-подхода.

Почему:
1. Цель `Mailbox2Slot` — 1P/1C, latest-wins, минимальный overhead.
2. Добавление 3-го слота не убирает само по себе SMP-гонки; всё равно нужен корректный протокол claim/publish и memory-order инварианты.
3. После добавления claim-маски/выбора свободного слота протокол действительно начинает концептуально сближаться с `SPMCSnapshot` (только с 1 читателем), но остаётся дороже по состояниям и проверкам, чем односоктовый seq-вариант.

Практический вывод:
- Для `Mailbox2SlotSmp` как базу беру 1-slot seq-вариант (writer wait-free, reader wait-free per call / lock-free в общем смысле, O(1), без mutex).
- 3-slot имеет смысл только если в требованиях появится отдельная цель: снизить вероятность `false` на читателе при длинном copy(T) и высокой частоте publish().
  Тогда это уже отдельный дизайн-профиль, ближе к snapshot-family, и его лучше оформлять отдельным типом, а не перегружать базовый `Mailbox2SlotSmp`.


[Сессия 0039][Оператор]

Не нравится мне однослотовая модель. Вероятность фейлов высока.
Клод?

[Сессия 0041][Клод]
Оператор прав. На SMP с 1 слотом писатель работает на другом ядре непрерывно,
поэтому любое копирование T рискует пересечься с записью. False-rate неприемлем.

Предлагаю: **2 слота + per-slot seqlock, читатель возвращает false вместо retry.**

Протокол:

    Писатель (wait-free):
        j = published.load(relaxed) ^ 1       // выбрать неопубликованный слот
        seq[j].fetch_add(1, release)           // открыть запись (seq нечётный)
        slots[j].value = value
        seq[j].fetch_add(1, release)           // закрыть запись (seq чётный)
        published.store(j, release)

    Читатель (одна попытка, wait-free per invocation):
        i = published.load(acquire)
        seq1 = seq[i].load(acquire)
        if seq1 % 2 != 0: return false         // писатель пишет в этот слот? (см. ниже)
        out = slots[i].value                   // копировать
        seq2 = seq[i].load(acquire)
        if seq1 != seq2: return false          // писатель вошёл в слот i во время копии
        return true

Почему false-rate низкий:
Писатель всегда пишет в слот j = published^1 — НЕ в текущий published слот.
Чтобы испортить копирование читателя (слот i = published), писатель должен:
    цикл 1: записать в j ≠ i, опубликовать j
    цикл 2: записать в i (теперь j = i) — вот здесь коллизия
Для RT систем с ограниченной частотой write это событие исключительно редко.
На практике: false только при двух полных циклах publish() за время copy(T).

Почему SMP-корректен:
    published.store(j, release) после seq[j] закрыт (чётный) гарантирует:
    любой читатель, кто загрузил published=j через acquire-load,
    видит seq[j] как чётный и слот j полностью записанным.
    Если писатель войдёт в слот i во время копирования — seq[i] изменится,
    читатель поймает это при re-verify.

Сравнение с вариантами:

    1-slot seqlock:     false при ЛЮБОМ publish() во время copy → высокий false-rate
    2-slot + seqlock:   false только при 2 циклах → в разы реже
    3-slot (SPMC N=1):  false никогда (писатель не может коснуться claimed-слота)
                        но: claim-протокол сложнее и дороже

2 слота + seqlock — оптимальный баланс: простота протокола, низкий false-rate,
те же 2 слота что в оригинальном Mailbox2Slot, без claim-verify, без preemption_disable.

Предлагаю принять это как базу для Mailbox2SlotSmp.

Примечание про "if seq1 % 2 != 0: return false":
По протоколу published флипается только ПОСЛЕ закрытия seq[j].
Поэтому в штатном сценарии seq[i] всегда чётный в момент загрузки published=i.
Нечётный seq[i] при published=i — теоретически возможен только при race
между flip published и momentary load (видимость на SMP). Проверка оставлена
как защитная — корректности не нарушает, лишнего false не добавляет в steady-state.

[Сессия 0042][Кодекс]
Проверил 0041. Поддерживаю переход на `2-slot + per-slot seqlock` как базу для `Mailbox2SlotSmp`.

Согласен по сути:
1. Это снимает главный риск 1-slot модели (высокий false-rate при любой concurrent publish).
2. Сохраняет простую 1P/1C топологию и writer wait-free.
3. Даёт практически более низкий false-rate: для коллизии нужен второй publish-цикл до завершения copy читателя.

Технические уточнения для контракта (чтобы не было разночтений в коде):
1. `published` должен быть бинарным индексом (`0/1`) без состояния `NONE` после первой публикации; до первой публикации — отдельный флаг `has_value` или sentinel.
2. Быстрый путь writer:
   - `j = published ^ 1`
   - `seq[j]` odd/even
   - `slot[j] = value`
   - `published.store(j, release)`
3. Читатель:
   - `i = published.load(acquire)`
   - `seq1 = seq[i].load(acquire)`; если odd -> false
   - copy
   - `seq2 = seq[i].load(acquire)`; если `seq1 != seq2` -> false, иначе true
4. Гарантии прогресса фиксируем явно:
   - writer: wait-free O(1)
   - reader: wait-free per invocation (single-shot), lock-free по потоку вызовов.

Итог: принимаю 2-slot seqlock-вариант как целевой для `Mailbox2SlotSmp`.

[Сессия 0043][Оператор]
Клод, что в консенсусе, что осталось?

[Сессия 0044][Клод]
Обновляю консенсус. Всё зафиксированное — с сессий 0028–0042.

---

[Консенсус — финальный, v2]

**Что не меняем:**
- `SPSCRing` — уже SMP-корректен (acquire/release на head_/tail_, без preemption_disable).
- Все три UP-only примитива остаются в кодовой базе, маркируются явно в заголовках:
  `// CONTRACT: UP-only (single-core + preemptive). Not safe on SMP.`

**Три новых SMP-примитива:**

1. `DoubleBufferSeqLock`
   Топология: 1P / 1C.
   Протокол: глобальный seq-счётчик. Писатель: seq++ (нечётный) → write → seq++ (чётный).
   Читатель: retry-loop: seq1 → copy → seq2; если seq1 нечётный или seq1≠seq2 — повторить.
   Прогресс: writer wait-free; reader lock-free (retry при конкуренции).

2. `Mailbox2SlotSmp`
   Топология: 1P / 1C.
   Протокол: 2 слота + per-slot seq.
   Писатель: j=published^1; seq[j]++ (нечётный); write; seq[j]++ (чётный); published=j.
   Читатель (single-shot, no retry): i=published; seq1=seq[i]; если нечётный→false;
   copy; seq2=seq[i]; если seq1≠seq2→false; иначе true.
   До первой публикации: отдельный флаг has_value (published не несёт NONE-состояния).
   Прогресс: writer wait-free O(1); reader wait-free per invocation, lock-free по потоку.
   False-rate: низкий — только при 2 полных publish()-циклах за время copy(T).

3. `SPMCSnapshotSmp`
   Топология: 1P / NC (N≤30).
   Протокол: fetch_or(1<<i) + re-verify + optional fetch_and на busy_mask.
   Читатель (single-shot):
       i = load(published, acq)
       busy_mask.fetch_or(1<<i, acq_rel)
       i2 = load(published, acq)
       если i2≠i: busy_mask.fetch_and(~(1<<i), rel); return false
       copy; release claim
   Прогресс: writer wait-free O(1); reader wait-free per invocation.

**Документация:**
- `primitives_README.md`: добавить матрицу UP-safe / SMP-safe для всех примитивов.
- Для каждого нового SMP-типа: контракт по шаблону Кодекса (сессия 0037):
  topology / progress guarantees / API / memory model / invariants / failure semantics / cost model.

**Тесты:**
- Стресс-тест для каждого SMP-примитива: многопоточный раннер, проверка torn-read и ABA.

---

**Что осталось (порядок работ):**

Шаг 1. Маркировка UP-only в заголовках трёх примитивов + матрица в README.
Шаг 2. Контракты: Кодекс пишет черновики по шаблону → обсуждение → фиксация.
Шаг 3. Реализация: DoubleBufferSeqLock → SPMCSnapshotSmp → Mailbox2SlotSmp.
Шаг 4. Стресс-тесты для каждого нового примитива.
Шаг 5. Унификация интерфейсов всех примитивов (минимальный overhead при связывании каналов).


[Сессия 0043][Оператор]
Клод меняет Маркировку UP-only в заголовках трёх примитивов + матрицу в README согласно шага 1.
Кодекс проверяет.

[Сессия 0045][Клод]
Шаг 1 выполнен. Отчёт.

Заголовки — добавлен блок PLATFORM CONSTRAINT:

  dbl_buffer.hpp — перед SAFETY NOTE:
      PLATFORM CONSTRAINT: UP-only (single-core + preemptive). Not safe on SMP.
      For SMP use DoubleBufferSeqLock (pending).

  mailbox2slot.hpp — перед PREEMPTION SAFETY:
      PLATFORM CONSTRAINT: UP-only (single-core + preemptive). Not safe on SMP.
      For SMP use Mailbox2SlotSmp (pending).

  spmc_snapshot.hpp — перед Validity conditions:
      PLATFORM CONSTRAINT: UP-only / Condition B SMP only.
      For general SMP use SPMCSnapshotSmp (pending).

  spsc_ring.hpp — не трогал (SMP-корректен).

primitives_README.md — четыре изменения:

  1. Добавлена секция SPMCSnapshot (отсутствовала).
  2. В таблицу Semantic Comparison добавлена строка SPMCSnapshot.
  3. Добавлена таблица SMP Safety Matrix:
       DoubleBuffer  | UP-safe ✓ | SMP-safe ✗
       Mailbox2Slot  | UP-safe ✓ | SMP-safe ✗
       SPMCSnapshot  | UP-safe ✓ | SMP-safe ✗/cond.
       SPSCRing      | UP-safe ✓ | SMP-safe ✓
  4. Секция SMP And Multi-Core Limitations расширена: добавлен SPMCSnapshot,
     каждый примитив явно помечен UP-only или SMP-safe,
     добавлены ссылки на pending SMP-варианты.

[Сессия 0046][Кодекс]
Проверил отчёт 0045 по фактическим файлам. Подтверждаю: шаг 1 выполнен корректно.

Проверка:
1. `dbl_buffer.hpp` — блок `PLATFORM CONSTRAINT` добавлен, ссылка на `DoubleBufferSeqLock (pending)` есть.
2. `mailbox2slot.hpp` — блок `PLATFORM CONSTRAINT` добавлен, ссылка на `Mailbox2SlotSmp (pending)` есть.
3. `spmc_snapshot.hpp` — блок `PLATFORM CONSTRAINT` добавлен (`UP-only / Condition B SMP`), ссылка на `SPMCSnapshotSmp (pending)` есть.
4. `spsc_ring.hpp` — без изменений (как и согласовано).
5. `primitives/primitives_README.md`:
   - секция `SPMCSnapshot` присутствует;
   - в `Semantic Comparison` строка `SPMCSnapshot` есть;
   - `SMP Safety Matrix` добавлена;
   - `SMP And Multi-Core Limitations` расширена по всем примитивам.

Одно замечание на доработку (док-несоответствие, не блокер шага 1):
- В `primitives_README.md` раздел `Common Requirements For All Primitives` содержит пункт
  "Exactly one producer and one consumer (SPSC)", что противоречит `SPMCSnapshot` (1P/NC).
  Предлагаю на шаге 2 разнести этот блок на:
  - общие требования для всех;
  - topology-specific требования (SPSC / SPMC).


[Сессия 0046][Оператор]
Кодекс пишет черновики контрактов по шаблону → обсуждение → фиксация и меняет primitives_README.md согласно его замечанию

[Сессия 0047][Кодекс]
Принял задачу. Подтверждаю старт шага 2:
1. Готовлю черновики контрактов по шаблону (topology / progress / API / memory model / invariants / failure semantics / cost model) для:
   - `DoubleBufferSeqLock`
   - `SPMCSnapshotSmp`
   - `Mailbox2SlotSmp`
2. Параллельно правлю `primitives/primitives_README.md` по замечанию 0046:
   - разделяю "общие требования" и "топология-специфичные требования (SPSC/SPMC)".

После внесения черновиков и правки README дам отдельный отчёт по каждому пункту для обсуждения и фиксации.

[Сессия 0048][Кодекс]
Шаг 2 исполнен: черновики контрактов подготовлены + `primitives_README.md` исправлен по замечанию 0046.

Сделано:

1. Добавлены черновики контрактов (по шаблону: topology / progress / API / memory model / invariants / failure semantics / cost model):
   - `primitives/docs/DoubleBufferSeqLock - RT Contract & Invariants.md`
   - `primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md`
   - `primitives/docs/Mailbox2SlotSmp - RT Contract & Invariants.md`

2. Исправлен `primitives/primitives_README.md`:
   - раздел `Common Requirements For All Primitives` приведён к реально общим требованиям;
   - добавлен отдельный подраздел `Topology-Specific Requirements`:
     - SPSC: `DoubleBuffer`, `Mailbox2Slot`, `SPSCRing`
     - SPMC: `SPMCSnapshot` (1P / NC).

Ключевые решения, зафиксированные в черновиках:

1. `DoubleBufferSeqLock`:
   - writer wait-free O(1), reader lock-free (retry-loop);
   - seq odd/even протокол с verify у читателя.

2. `SPMCSnapshotSmp`:
   - writer wait-free O(1), reader wait-free per invocation;
   - single-shot reader claim: `fetch_or + re-verify + optional fetch_and`.

3. `Mailbox2SlotSmp`:
   - выбран базовый протокол `2 slots + per-slot seq`;
   - writer wait-free O(1);
   - reader single-shot `try_read` (wait-free per invocation), `false` при race;
   - добавлен `has_value` для фазы до первой публикации.

Что прошу подтвердить перед фиксацией:
1. Именование новых документов (`...Smp`, `...SeqLock`) устраивает.
2. Для `DoubleBufferSeqLock` сохраняем retry-loop в `read()` (а не single-shot false).
3. Для `Mailbox2SlotSmp` оставляем именно `2-slot + per-slot seq` как baseline.

После подтверждения перейду к следующему шагу (реализация в порядке: `DoubleBufferSeqLock` → `SPMCSnapshotSmp` → `Mailbox2SlotSmp`).

[Сессия 0049][Оператор]

Клод, замечания к черновикам???

[Сессия 0050][Клод]
Прочитал все три черновика. Два из трёх — без блокеров. Один — критическая проблема.

---

DoubleBufferSeqLock — без блокеров. Два уточнения:

1. В §1/§2 нет явного указания "SMP-safe: yes". Нужно добавить в topology-раздел.

2. Поведение до первой write() не задокументировано в §6.
   По аналогии с оригинальным DoubleBuffer: read() вернёт нулевую копию T.
   "No data yet" семантически неотличимо от "valid zero snapshot". Стоит зафиксировать.

3. (Пояснение, не требует правки) Если writer делает 2 цикла пока reader копирует —
   данные в буфере меняются во время copy(T). Это data race по C++ ISO.
   Seqlock принимает это как допустимый компромисс: torn-копия обнаруживается
   seq-верификацией и отбрасывается. Для RT-систем с trivially copyable T — приемлемо.
   Рекомендую добавить NOTE в §5 Invariants: явно назвать это "known seqlock trade-off".

---

SPMCSnapshotSmp — КРИТИЧЕСКАЯ ПРОБЛЕМА: отсутствует refcnt для N>1.

При N=1 черновик корректен. При N>1 — баг протокола:

Сценарий (N=2, readers A и B оба claim slot i):
  1. A: fetch_or(1<<i) → busy_mask[i] = 1
  2. B: fetch_or(1<<i) → busy_mask[i] = 1 (бит уже стоит, но OK)
  3. A: копирует slots[i]
  4. A: ЗАВЕРШАЕТ — fetch_and(~(1<<i)) → busy_mask[i] = 0  ← бит снят!
  5. Writer: видит busy_mask[i] = 0, published ≠ i → выбирает j = i → ПИШЕТ в slots[i]
  6. B: всё ещё копирует slots[i] → TORN READ

Оригинальный SPMCSnapshot решает это через refcnt[i]:
только последний reader (fetch_sub возвращает 1) очищает бит в busy_mask.

Это необходимо перенести и в SPMCSnapshotSmp:
  RELEASE claim (шаг 5 в reader):
    if (refcnt[i].fetch_sub(1, acq_rel) == 1)
        busy_mask.fetch_and(~(1<<i), release);

Без этого SPMCSnapshotSmp корректен только для N=1.
Блокер для фиксации черновика, нужна правка.

Дополнительно: в §4 reader-протоколе отсутствует шаг "if !initialized → return false"
(есть в §6 Failure Semantics, но не в §4 Memory Model). Нужно добавить.

---

Mailbox2SlotSmp — без блокеров. Одно пояснение:

Проверка "if seq[i] odd → return false" (шаг 3 reader) в штатном сценарии
недостижима по инварианту протокола:
  Writer публикует `published = j` только ПОСЛЕ seq[j].fetch_add(→even, release).
  Reader загружает published = j через acquire-load, что устанавливает
  happens-before с writer's release — reader всегда видит seq[j] как чётный.

Проверка остаётся как defensive guard (допустимо, overhead нулевой).
Рекомендую добавить в §5 Invariants: "seq[published] is always even at the moment
reader loads published via acquire. The odd-guard in try_read() is defensive."

Общий вывод: DoubleBufferSeqLock и Mailbox2SlotSmp — готовы к фиксации с минорными правками.
SPMCSnapshotSmp — нужна правка refcnt перед фиксацией.

[Сессия 0051][Кодекс]
Проверил 0050. Подтверждаю выводы Клода.

Статус по черновикам:
1. `DoubleBufferSeqLock` — не блокирует, принимаю 2 доработки:
   - явно добавить `SMP-safe: yes` в topology/constraints;
   - зафиксировать поведение `read()` до первой `write()` (семантически неотличимо от valid zero-snapshot).
   - NOTE про seqlock trade-off (возможна torn-копия, отбрасывается verify) — добавить.

2. `SPMCSnapshotSmp` — блокер подтверждаю:
   - без `refcnt` протокол корректен только для `N=1`;
   - для `N>1` возможен ранний clear `busy_mask` одним reader при активном втором reader.
   Перед фиксацией правим черновик:
   - возвращаем `refcnt[i]`;
   - release claim: clear `busy_mask` только на переходе `refcnt: 1 -> 0`;
   - в §4 явно добавляем шаг `if !initialized -> return false`.

3. `Mailbox2SlotSmp` — без блокеров; принимаю пояснение:
   - `odd` guard оставить как defensive check;
   - в инвариантах явно зафиксировать: при `published`-чтении через acquire штатно наблюдается even `seq[published]`.

Предлагаю так: вношу эти правки в 3 черновика и после этого отдаю обновлённый вариант на финальную фиксацию консенсуса шага 2.

[Сессия 0052][Кодекс]
Правки внесены. Обновлены все 3 черновика контрактов согласно замечаниям 0050/0051.

1. `DoubleBufferSeqLock - RT Contract & Invariants.md`
   - Добавлено явное `Platform target: SMP-safe` в topology.
   - В `Failure/Retry Semantics` добавлено поведение до первой `write()`:
     возвращается value-initialized content; "no data yet" семантически неотличимо от valid zero snapshot.
   - В `Invariants` добавлен NOTE про seqlock trade-off:
     возможна torn промежуточная копия при overlap, но она отбрасывается seq-верификацией.

2. `SPMCSnapshotSmp - RT Contract & Invariants.md`
   - Закрыт блокер по `N>1`: добавлен `refcnt[i]` в reader-протокол.
   - Reader claim/release теперь:
     `fetch_or(busy_mask)` -> `fetch_add(refcnt)` -> re-verify ->
     on release `fetch_sub(refcnt)` и clear busy только при переходе `1 -> 0`.
   - В §4 добавлен явный шаг `if !initialized -> return false`.
   - В `Invariants` добавлены пункты про связь `refcnt` и `busy_mask`.
   - `Cost model` обновлён с учётом `refcnt` RMW.

3. `Mailbox2SlotSmp - RT Contract & Invariants.md`
   - В `Invariants` добавлено уточнение:
     при `published.load(acquire)` в steady-state ожидается even `seq[published]`;
     odd-check в reader сохранён как defensive guard.

Итог:
- Критический блокер по `SPMCSnapshotSmp` устранён.
- Черновики готовы к финальной проверке и фиксации консенсуса шага 2.

[Сессия 0053][Клод]
Прочитал все три обновлённых черновика. Все замечания 0050 закрыты. Протоколы корректны.

Верификация SPMCSnapshotSmp (основная проблема):

Обновлённый reader-протокол:
    3. fetch_or(busy_mask[i], acq_rel)     // claim: busy BEFORE refcnt
    4. fetch_add(refcnt[i], acq_rel)       // confirm count
    5. i2 = load(published, acq)           // re-verify
    6. if i2 != i: fetch_sub(refcnt) → if 1→0: clear busy; return false
    7. copy slots[i]
    8. fetch_sub(refcnt) → if 1→0: clear busy; return true

Инвариант I5 сохранён: busy_mask[i] очищается только последним reader (refcnt: 1→0).
Порядок claim: busy РАНЬШЕ refcnt — гарантирует что writer видит занятость до подтверждения.
Порядок release: refcnt РАНЬШЕ busy — гарантирует что refcnt=0 до снятия бита.

Проверил дополнительный сценарий (N=2, Reader A прерывается между step 3 и step 4):
- Reader B полностью завершается (refcnt: 1→0, bit cleared)
- Writer может снова выбрать slot i, ТОЛЬКО если published ≠ i (I3)
- Reader A при re-verify (step 5) увидит изменённый published → false
- Разрыв чтения невозможен: Reader A копирует только если published == i при step 5,
  что по I3 блокирует писателя от выбора slot i
Протокол корректен для N>1. ✓

Одно минорное замечание (не блокер, рекомендация):
SPMCSnapshotSmp §1 Topology не имеет явного "Platform target: SMP-safe",
в отличие от DoubleBufferSeqLock. Стоит добавить при финализации для единообразия.

Ответы на вопросы Кодекса из сессии 0048:
1. Именование (Smp / SeqLock) — принимаю. DoubleBufferSeqLock подчёркивает retry-семантику.
2. DoubleBufferSeqLock: retry-loop в read() — да, оставляем (lock-free, consense зафиксирован).
3. Mailbox2SlotSmp: 2-slot + per-slot seq — да, оставляем как baseline.

Шаг 2 завершён. Черновики готовы к фиксации. Можно переходить к шагу 3 — реализации.

[Сессия 0054][Кодекс]
Проверил и финализировал контракты шага 2.

Что финализировано:
1. `primitives/docs/DoubleBufferSeqLock - RT Contract & Invariants.md`
   - снят статус Draft;
   - `Revision` обновлён до `1.0 (March 2026)`.

2. `primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md`
   - снят статус Draft;
   - `Revision` обновлён до `1.0 (March 2026)`;
   - добавлена явная пометка в topology: `Platform target: SMP-safe`.

3. `primitives/docs/Mailbox2SlotSmp - RT Contract & Invariants.md`
   - снят статус Draft;
   - `Revision` обновлён до `1.0 (March 2026)`;
   - добавлена явная пометка в topology: `Platform target: SMP-safe`.

Итог:
- Контракты SMP-примитивов зафиксированы в финальном виде (v1.0).
- Шаг 2 закрыт. Готов переходить к шагу 3 (реализация в порядке: `DoubleBufferSeqLock` → `SPMCSnapshotSmp` → `Mailbox2SlotSmp`).

[Сессия 0055][Оператор]

Клод реализуешь Шаг 3. Реализация: DoubleBufferSeqLock → SPMCSnapshotSmp → Mailbox2SlotSmp.
[Сессия 0056][Клод]
Шаг 3 выполнен. Реализованы три SMP-примитива.

**Созданные файлы:**

1. `primitives/include/stam/primitives/dbl_buffer_seqlock.hpp`
   - `DoubleBufferSeqLockCore<T>` — 2 cacheline-aligned секции: `SeqLine` (seq) + `Slot` (value)
   - `DoubleBufferSeqLockWriter<T>` — `write()`: wait-free, O(1), 2x `fetch_add(1, release)` вокруг payload copy
   - `DoubleBufferSeqLockReader<T>` — `read()`: lock-free, retry-loop, 2x `load(acquire)` + copy per attempt
   - `DoubleBufferSeqLock<T>` — convenience wrapper

2. `primitives/include/stam/primitives/spmc_snapshot_smp.hpp`
   - Структура идентична `SPMCSnapshot` (K=N+2 slots, busy_mask, published, initialized)
   - Добавлен `refcnt[K]` (критический, был missing в черновике 0050)
   - Reader-протокол (wait-free per call):
     `initialized check` → `load(published=i)` → `fetch_or(busy[i])` → `fetch_add(refcnt[i])` →
     `re-verify published` → при изменении: release + return false →
     `copy slots[i]` → release (fetch_sub; if 1→0: fetch_and(busy))
   - Нет `preemption_disable`. Инвариант I5 (busy перед refcnt при claim; refcnt до busy при release) сохранён.
   - `detail::ctz32_smp` — переименован чтобы избежать конфликта при совместном включении с `spmc_snapshot.hpp`

3. `primitives/include/stam/primitives/mailbox2slot_smp.hpp`
   - `Mailbox2SlotSmpCore<T>` — slots[2] (отдельные cacheline) + seqs[2] (per-slot, отдельные cacheline) + ctrl (published + has_value)
   - Writer-протокол (wait-free, O(1)):
     `j = published^1` → `seq[j].fetch_add(1, release)` (odd) → write slots[j] →
     `seq[j].fetch_add(1, release)` (even) → `published.store(j, release)` → `has_value.store(true, release)`
   - Reader-протокол (wait-free per call, single-shot):
     `has_value check` → `i = published.load(acquire)` → `s1 = seq[i].load(acquire)` →
     if odd: false → `copy slots[i]` → `s2 = seq[i].load(acquire)` → if s1≠s2: false → true
   - Defensive odd-check на step 3 сохранён (как зафиксировано в контракте)

**Обновлён `primitives_README.md`:**
- Добавлены секции для 3 новых примитивов
- Semantic Comparison расширена (7 строк)
- SMP Safety Matrix обновлена (строки "pending" заменены на реализованные)
- Topology-Specific Requirements: добавлены SMP-варианты в список

Все три реализации следуют контрактам Rev 1.0 (шаг 2).
Шаг 3 завершён. Готов к шагу 4 — stress tests.


[Сессия 0056][Оператор]

Меняем порядок. Сначала унификация контрактов, потом стресс-тесты.

Кодекс, проверь реализации, контракты.

[Сессия 0057][Кодекс]
Проверил реализации (`dbl_buffer_seqlock.hpp`, `spmc_snapshot_smp.hpp`, `mailbox2slot_smp.hpp`) и финальные контракты Rev 1.0.

Итог проверки:
1. `DoubleBufferSeqLock` — реализован в соответствии с контрактом:
   - writer wait-free (2x seq RMW + copy),
   - reader retry-loop (lock-free),
   - SMP-safe пометки и seqlock trade-off отражены.

2. `SPMCSnapshotSmp` — критический блокер закрыт:
   - `refcnt[K]` присутствует,
   - claim/release порядок корректный (`busy -> refcnt` при claim, `refcnt -> busy` при release),
   - clear busy только на переходе `refcnt: 1 -> 0`,
   - `initialized` gate в `try_read()` присутствует.

3. `Mailbox2SlotSmp` — соответствует baseline `2-slot + per-slot seq`:
   - writer wait-free O(1),
   - reader single-shot try_read,
   - defensive odd-check сохранён и документирован.

4. `primitives/primitives_README.md` — структура и матрицы обновлены корректно:
   - добавлены новые SMP-примитивы,
   - topology-specific требования разделены.

Минорные замечания (док-унификация, не блокеры):
1. В разделе `SMP And Multi-Core Limitations` остались формулировки вида
   "`Use ... (pending)`" для `DoubleBufferSeqLock` / `Mailbox2SlotSmp` / `SPMCSnapshotSmp`,
   хотя эти варианты уже реализованы.
2. В `SMP Safety Matrix` строка `Mailbox2SlotSmp` сейчас говорит "both sides wait-free per invocation";
   формально корректнее: writer wait-free, reader wait-free per invocation (single-shot).

Вывод:
- Реализация шага 3 технически согласована с контрактами.
- Можно переходить к следующей задаче: унификация интерфейсов примитивов, затем stress-тесты.

[Сессия 0058][Кодекс]
Замечания 0057 поправил.

Изменён файл: `primitives/primitives_README.md`

Что исправлено:
1. В `SMP And Multi-Core Limitations` убраны устаревшие пометки `(pending)`:
   - `DoubleBufferSeqLock`
   - `Mailbox2SlotSmp`
   - `SPMCSnapshotSmp`

2. В `SMP Safety Matrix` уточнена формулировка для `Mailbox2SlotSmp`:
   - было: "both sides wait-free per invocation"
   - стало: "writer wait-free, reader wait-free per invocation"

Итог:
- Документация приведена в соответствие с текущим состоянием реализации.
- Блокирующих замечаний по шагу 3/унификации контрактов не осталось.

[Сессия 0059][Клод]
Принял изменения. Шаг 3 закрыт, README актуален.

Следующий шаг: унификация интерфейсов (шаг 5 консенсуса), сдвинутый вперёд оператором.

**Что понимаю под унификацией:**
В оригинальном плане (сессия 0044): "унификация интерфейсов всех примитивов (минимальный overhead для channel binding)".

Сейчас у нас 3 разных "стиля" именования:
- `write()`/`read()` — DoubleBuffer, DoubleBufferSeqLock
- `publish()`/`try_read()` — Mailbox2Slot, Mailbox2SlotSmp, SPMCSnapshot, SPMCSnapshotSmp
- `push()`/`pop()` — SPSCRing

Вопрос к оператору и Кодексу: что конкретно унифицировать?

**Вариант A — только именование операций:**
Привести все snapshot-примитивы к единому API:
`write()` / `try_read()` или `publish()` / `try_read()`.
SPSCRing оставить с `push()`/`pop()` (другая семантика — FIFO).

**Вариант B — концепт/trait для channel binding:**
Ввести концепты `Writable<T>` и `Readable<T>` (или аналогичный тег-механизм),
чтобы channel binding в registry мог работать с любым примитивом через единый интерфейс
без условной компиляции и switch по типу.
Это даёт "минимальный overhead" через статический полиморфизм (zero-cost abstraction).

**Вариант C — оба:**
A + B.

Моя позиция: если целевой use-case — channel binding в реестре, нужен вариант B.
Именование (вариант A) опционально — можно добавить алиасы не ломая текущий код.

Кодекс, оператор: какой вариант? Есть ли конкретный интерфейс, к которому нужно прийти?


[Сессия 0043][Оператор]

Все методы приводим к состояния `write()` / `try_read()` Оптимизация пр варианту А необходима.

Ввести концепты `Writable<T>` и `Readable<T> - не понял, объясните.

[Сессия 0060][Кодекс]
Принял. Фиксирую решение: делаем **вариант A** — унификация API к `write()` / `try_read()`.

Кратко про `Writable<T>` / `Readable<T>` (только пояснение, без внедрения сейчас):
- Это compile-time проверки (концепты C++20), что тип имеет нужные методы.
- Пример идеи:
  - `Writable<C, T>`: у канала `C` есть `write(const T&) noexcept`
  - `Readable<C, T>`: у канала `C` есть `try_read(T&) noexcept -> bool`
- Зачем это бывает нужно: чтобы generic-код (binding/registry) компилировался только для совместимых каналов.

Но по вашему решению сейчас это **не делаем**. Делаем только унификацию методов (A) с минимальным overhead и без лишних абстракций.

Готов перейти к правкам интерфейсов по шагу 5.

[Сессия 0061][Кодекс]
Выполнил унификацию интерфейсов по варианту A (`write()` / `try_read()`) с сохранением обратной совместимости.

Кодовые правки:
1. `primitives/include/stam/primitives/mailbox2slot.hpp`
   - в `Mailbox2SlotWriter` добавлен alias:
     `void write(const T& value) noexcept { publish(value); }`

2. `primitives/include/stam/primitives/mailbox2slot_smp.hpp`
   - в `Mailbox2SlotSmpWriter` добавлен alias:
     `void write(const T& value) noexcept { publish(value); }`

3. `primitives/include/stam/primitives/spmc_snapshot.hpp`
   - в `SPMCSnapshotWriter` добавлен alias:
     `void write(const T& value) noexcept { publish(value); }`

4. `primitives/include/stam/primitives/spmc_snapshot_smp.hpp`
   - в `SPMCSnapshotSmpWriter` добавлен alias:
     `void write(const T& value) noexcept { publish(value); }`

5. `primitives/include/stam/primitives/dbl_buffer.hpp`
   - в `DoubleBufferReader` добавлен unified метод:
     `bool try_read(T& out) const noexcept` (всегда `true`, вызывает `read(out)`).

6. `primitives/include/stam/primitives/dbl_buffer_seqlock.hpp`
   - в `DoubleBufferSeqLockReader` добавлен unified метод:
     `bool try_read(T& out) noexcept` (всегда `true`, вызывает `read(out)`).

Документация:
7. `primitives/primitives_README.md`
   - добавлена явная секция "Snapshot API unification (v1)":
     canonical: `write()/try_read()`, legacy aliases (`publish()/read()`) сохранены.

Проверка:
- `cmake --build /tmp/build -j4` — успешно, без ошибок (есть прежние не-блокирующие warnings в старых тестах).

Итог:
- Канонический API `write()/try_read()` доступен во всех snapshot-примитивах.
- Старые вызовы не сломаны (compatibility сохранена).


[Сессия 0061][Оператор]

Оптимизируем по варианту **Вариант C — оба:**
Кодекс, проведи оптимизацию.

[Сессия 0062][Клод]
Вариант A выполнен корректно — алиасы без overhead, совместимость сохранена.

Перед реализацией концептов (вариант B) уточняю архитектуру:

**Предлагаемые концепты (C++20):**

```cpp
// SnapshotWriter<W, T>: тип W — пишущая сторона snapshot-канала для значений T.
template<typename W, typename T>
concept SnapshotWriter = requires(W w, const T& v) {
    { w.write(v) } noexcept -> std::same_as<void>;
};

// SnapshotReader<R, T>: тип R — читающая сторона snapshot-канала для значений T.
template<typename R, typename T>
concept SnapshotReader = requires(R r, T& out) {
    { r.try_read(out) } noexcept -> std::same_as<bool>;
};
```

**Размещение:** новый файл `primitives/include/stam/primitives/snapshot_concepts.hpp`.
Не включать в отдельные примитивы — каждый заголовок независим.

**SPSCRing:** не включать в эти концепты. `push()`/`pop()` — FIFO-семантика, отдельный набор концептов при необходимости.

**Статус проверки алиасов:**
Все 6 правок Кодекса из сессии 0061 технически корректны. Алиасы — inline, zero-overhead.
Единственный момент: в `DoubleBuffer` и `DoubleBufferSeqLock` reader `try_read()` всегда возвращает `true`.
Это задокументированное отличие от `Mailbox2Slot*` — caller должен учитывать.
Для channel binding это важно: generic-код, ожидающий `false` при "нет данных", должен знать семантику.
Если нужна однородность семантики `false` = "нет данных" — `DoubleBuffer*` не подходит напрямую.
Если такой однородности не требуется — всё нормально.

Кодекс: реализуй `snapshot_concepts.hpp` по предложенной схеме выше?

[Сессия 0063][Оператор]

Принято. Кодекс, вноси правки.

[Сессия 0064][Кодекс]
Вариант C (часть B: концепты) реализован.

Сделано:
1. Добавлен новый файл:
   - `primitives/include/stam/primitives/snapshot_concepts.hpp`

2. В файле зафиксированы C++20-концепты:
   - `SnapshotWriter<W, T>`: требует `w.write(const T&) noexcept -> void`
   - `SnapshotReader<R, T>`: требует `r.try_read(T&) noexcept -> bool`

3. Обновлён `primitives/primitives_README.md`:
   - в секцию `Snapshot API unification (v1)` добавлена ссылка на
     `snapshot_concepts.hpp` и имена концептов.

Проверка:
- `cmake --build /tmp/build -j4` — успешно.

Итог:
- Вариант A (унифицированные методы) + вариант B (compile-time концепты)
  теперь реализованы вместе, без изменения runtime-семантики примитивов.

[Сессия 0065][Оператор]

Переходим к шагу "Стресс-тесты для каждого нового примитива.". Клод, это твоя реализация.

[Сессия 0067][Кодекс]
Проверил, что Клод успел по тестам, и закрыл недоделанные части.

Что было незавершено:
1. `dbl_buffer_seqlock_test.cpp` и `mailbox2slot_smp_test.cpp` уже существовали, но не были подключены в `stam_tests`.
2. Для третьего нового SMP-примитива отсутствовал тест-файл (`SPMCSnapshotSmp`).
3. `primitives/tests/main.cpp` не вызывал новые тестовые наборы.

Что доделано:
1. `primitives/tests/CMakeLists.txt`
   - добавлены в сборку:
     - `dbl_buffer_seqlock_test.cpp`
     - `mailbox2slot_smp_test.cpp`
     - `spmc_snapshot_smp_test.cpp` (новый файл)

2. `primitives/tests/main.cpp`
   - добавлены объявления и вызовы:
     - `dbl_buffer_seqlock_tests()`
     - `mailbox2slot_smp_tests()`
     - `spmc_snapshot_smp_tests()`

3. Добавлен новый файл:
   - `primitives/tests/spmc_snapshot_smp_test.cpp`
   - содержит функциональные и stress-тесты:
     - концепты `SnapshotWriter/SnapshotReader`
     - `try_read` до первой публикации
     - alias `write()`
     - cleanup `busy_mask/refcnt`
     - stress N=1, N=2 (no torn-read), sustained N=4 с проверкой cleanup

Проверка:
- `cmake --build /tmp/build -j4` — успешно.
- запуск `/tmp/build/primitives/tests/stam_tests` — успешно.
- все наборы тестов проходят, включая новые:
  - `DoubleBufferSeqLock`: 20/20 PASS
  - `Mailbox2SlotSmp`: 22/22 PASS
  - `SPMCSnapshotSmp`: 7/7 PASS

Итог:
- Шаг "стресс-тесты для каждого нового примитива" завершён полностью.

[Сессия 0068][Кодекс]
Выполнил операцию "мерджим всё в master" повторно с чистого состояния.

Что сделано:
1. Отменён текущий конфликтный merge в `master`:
   - `git merge --abort`
2. После отмены подтверждено чистое состояние ветки (merge-состояние снято).
3. Выполнен новый merge:
   - `git merge --no-ff dev`
   - результат: успешный merge-коммит `ea93107` (`Merge branch 'dev'`).

Текущее состояние:
- `master` содержит изменения из `dev`.
- `git status`: `master...origin/master [ahead 28]`.

Примечание:
- В рабочем дереве остались 3 untracked файла в `docs/concepts/...` (старые локальные артефакты с именами на кириллице/спецсимволах); они не входят в merge-коммит.

[Сессия 0066][Кодекс]
Проверил, что было сделано Клодом по стресс-тестам, и дополнил недостающее.

Что было не доделано:
1. Новые тесты `dbl_buffer_seqlock_test.cpp` и `mailbox2slot_smp_test.cpp` были в репозитории,
   но не были подключены к сборке `stam_tests`.
2. Отсутствовал тест-файл для третьего нового примитива: `SPMCSnapshotSmp`.
3. `main.cpp` тест-раннера не вызывал новые тестовые наборы.

Что доделано:
1. Подключил новые тесты в `primitives/tests/CMakeLists.txt`:
   - `dbl_buffer_seqlock_test.cpp`
   - `mailbox2slot_smp_test.cpp`
   - `spmc_snapshot_smp_test.cpp` (новый файл)

2. Обновил `primitives/tests/main.cpp`:
   - добавлены объявления и вызовы:
     - `dbl_buffer_seqlock_tests()`
     - `mailbox2slot_smp_tests()`
     - `spmc_snapshot_smp_tests()`

3. Добавил недостающий файл `primitives/tests/spmc_snapshot_smp_test.cpp`:
   - функциональные проверки:
     - концепты `SnapshotWriter/SnapshotReader`
     - `try_read` до первой публикации
     - alias `write()`
     - cleanup `busy_mask/refcnt`
   - stress-проверки:
     - `N=1` no torn-read
     - `N=2` no torn-read
     - sustained `N=4` + cleanup (`busy_mask==0`, `refcnt==0`)

Результат проверки:
- `cmake --build /tmp/build -j4` — успешно.
- запуск `/tmp/build/primitives/tests/stam_tests` — успешно.
- Все наборы тестов проходят, включая новые:
  - `DoubleBufferSeqLock`: 20/20 PASS
  - `Mailbox2SlotSmp`: 22/22 PASS
  - `SPMCSnapshotSmp`: 7/7 PASS

Итог:
- Шаг со стресс-тестами для всех трёх новых SMP-примитивов теперь завершён полностью.
