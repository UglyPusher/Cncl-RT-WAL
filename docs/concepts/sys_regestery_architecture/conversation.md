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