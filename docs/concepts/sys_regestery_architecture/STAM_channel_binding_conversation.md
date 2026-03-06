[Сессия 0000][Оператор]

Ваши соображения????

---

[Сессия 0001][Клод]

Прочитал task.md, зафиксированную документацию и код. Излагаю структуру проблемы и ключевые вопросы.

## 1. Что уже зафиксировано (и не требует обсуждения)

Из `STAM_DTO_chanel_structure.md` §4 и `STAM_SysRegistry_structure.md` §5:
- Порты (`InPort<T>`, `OutPort<T>`) объявляются как **public-поля payload-объекта задачи**
- `assign_port()` вызывается в bootstrap-фазе и **инжектирует handle примитива в слот payload**
- После `seal()` задача работает через pre-bound handles в своём `step(now)`

Это означает: **wrapper менять не нужно**. Wrapper вызывает `payload_.step(now)`, внутри step payload сам использует свои порты. Каналы привязываются к задаче, а не к C++-классу `TaskWrapper`.

## 2. Что не существует в коде (нужно создать)

Типы `InPort<T>` и `OutPort<T>` отсутствуют в `tags.hpp` и нигде в `stam-rt-lib/include/`.
Это и есть основная задача Фазы 1.

---

## 3. Ключевой вопрос: единые или раздельные типы портов?

Два канала имеют разные write-API:

| Канал | Write | Read |
|-------|-------|------|
| `EventChannel<T>` | `push(T) → bool` (FIFO, может не вместить) | `pop(T&) → bool` |
| `StateChannel<T>` | `publish(T) → void` (всегда успешно) | `try_read(T&) → bool` |

**Вариант A — единый `OutPort<T>` / `InPort<T>`:**
```cpp
// Payload объявляет:
OutPort<SensorData>  sensor_out;
InPort<ControlData>  control_in;
```
`OutPort<T>` содержит оба метода (`push` и `publish`); один из них ассертирует при вызове не для того типа канала. Или оба присутствуют, payload выбирает нужный.
Минус: тип порта не отражает семантику канала. Ошибка вызова `publish()` на EventChannel обнаруживается только в runtime (assert) или не обнаруживается вовсе.

**Вариант B — раздельные типы по виду канала:**
```cpp
// Payload объявляет:
EventOutPort<SensorData>  sensor_out;
StateInPort<ControlData>  control_in;
```
Плюс: тип является документацией. Компилятор запрещает `push()` на StateOutPort.
Минус: изменяет пример из зафиксированной документации (`OutPort<T>` → `EventOutPort<T>` / `StateOutPort<T>`).

**Мой голос: Вариант B.**
Раздельные типы — правильный выбор для embedded RT-системы, где цена ошибки высока. Изменение примера в документации — приемлемая цена за типовую безопасность. Зафиксированное решение гласит, что `OutPort<T>` — "направление является типовой гарантией"; логично расширить эту гарантию на семантику канала.

---

## 4. Ключевой вопрос: как `assign_port()` находит поле в payload?

В документации реестра написано:
```cpp
registry.assign_port("TASK_A", "OUT_X", channel, writer);
```
Строка `"OUT_X"` — имя порта. Но в C++ нет runtime reflection; payload — обычная struct.

**Варианты:**

**Вариант I — `assign_port()` принимает прямой указатель на поле:**
```cpp
registry.assign_port(task_desc, &payload.sensor_out, channel, writer_side);
```
Строка имени — только для диагностики в `SealError`, не для lookup.
Плюс: zero-overhead, простота, нет строкового поиска.
Минус: меняет зафиксированный API реестра.

**Вариант II — порт регистрирует себя по имени (самоописание):**
```cpp
// InPort / OutPort хранят имя (const char*), установленное при объявлении:
EventOutPort<T> sensor_out{"OUT_X"};
```
`assign_port("TASK_A", "OUT_X", ...)` ищет порт по имени в PortList задачи.
Но PortList нужно собирать при `add()` — payload должен предоставить список портов.
Плюс: соответствует зафиксированному API.
Минус: lookup при bootstrap (O(n), но это не RT — допустимо).

**Вариант III — PortList как статическая таблица:**
```cpp
// Payload предоставляет статический метод:
static PortList make_port_list(SensorPayload& p) {
    return { {"OUT_X", &p.sensor_out}, {"IN_Y", &p.control_in} };
}
```
Минус: шаблонный код на стороне разработчика задачи.

**Мой голос: Вариант I** как основной, если оператор готов скорректировать API реестра.
Вариант II — если API реестра менять нельзя.

---

## 5. Что нужно в `tags.hpp`

Минимально — концепты для портов (если хотим статическую проверку на уровне реестра):

```cpp
template<class Port, class T>
concept WritablePort = requires(Port& p, T val) {
    // EventOutPort: push; StateOutPort: publish
    // Концепт может быть раздельным для каждого вида
};
```

Если идём по Варианту B (раздельные типы), концепты `EventWritable<T>` / `StateWritable<T>` дают чистую статическую проверку при `assign_port()`.

Если не нужно — tags.hpp не трогаем вообще.

---

## 6. Итог: что обсуждаем

---

[Сессия 0002][Кодекс]

Проверил доступность файлов по `task.md`.

Доступны:
- `docs/concepts/sys_regestery_architecture/task.md`
- `docs/concepts/sys_regestery_architecture/STAM_channel_binding_conversation.md`
- `stam-rt-lib/include/exec/tasks/taskwrapper.hpp`
- `stam-rt-lib/include/model/tags.hpp`
- каталоги `docs/concepts/fixed` и `primitives/docs`

Не найдены:
- `docs/concepts/sys_regestery_architecture/conversation.md`
- `docs/concepts/sys_regestery_architecture/Primitives update conversation.md`

Недостаток информации: отсутствуют 2 файла обсуждений из вкладок IDE.
Статус: к работе по Фазе 1 готов.

Три вопроса, требующих решения перед Фазой 2:

1. **Типы портов**: единый `OutPort<T>` или раздельные `EventOutPort<T>` / `StateOutPort<T>`?
2. **API `assign_port()`**: прямой указатель на поле или строковый lookup?
3. **`tags.hpp`**: добавляем концепты для портов или нет?

На `taskwrapper.hpp` — скорее всего ноль изменений. Если оператор видит иначе — прошу пояснить.


[Сессия 0003][Оператор]

На пальцах объясните мне, как проходит привязка канала к порту задачи??? Кто привязывает?


[Сессия 0004][Кодекс]

На пальцах:

Кто привязывает:
- привязку делает код bootstrap (инициализация системы), который вызывает API реестра;
- фактическую запись handle в порт делает сам `registry.assign_port(...)` внутри реестра;
- `TaskWrapper` каналы не привязывает, он только вызывает `payload.step(now)` и ведёт heartbeat.

Как проходит привязка:
1. Разработчик задачи объявляет в payload порты (`InPort/OutPort`) как поля.
2. Создаётся `TaskWrapper(payload)` и задача регистрируется через `registry.add(task_desc, wrapper)`.
3. Создаётся канал (`EventChannel`/`StateChannel`).
4. В bootstrap вызывается `registry.assign_port(task_name, port_name, channel, side)`.
5. Реестр находит задачу и порт, проверяет направление (`writer->OutPort`, `reader->InPort`) и тип данных `T`.
6. Реестр берёт соответствующую сторону канала (writer/reader handle) и записывает её в слот порта payload.
7. После `seal()` конфигурация заморожена; дальше в runtime задача работает через уже привязанные порты.

Коротко по ответственности:
- Оператор/интегратор: вызывает `assign_port` в `init()`.
- Реестр: валидирует и выполняет привязку.
- Payload задачи: использует привязанный порт в `step()`.
- TaskWrapper: не участвует в канальной привязке.

Моё соображение по текущему состоянию:
- этот процесс зафиксирован в документации, но в коде ещё не реализованы сами типы `InPort/OutPort` и механизм их заполнения, это и есть предмет Фазы 1.


[Сессия 0005][Оператор]
"Реестр берёт соответствующую сторону канала (writer/reader handle) и записывает её в слот порта payload." - А не многовато мы функционала даем реестру??? Докопаться до пейлоад, туда положить интерфейс????.

Ваши соображения?

---

[Сессия 0006][Клод]

Оператор абсолютно прав. Реестр — пассивная конфигурационная таблица. Давать ему
право "залезть" в payload и записать туда handle — нарушение разделения ответственности.

## Кто должен делать привязку?

**Bootstrap-код.** Это его единственная работа: знать все компоненты системы и соединить их.
Реестр — только валидатор и таблица топологии.

## Предлагаемая схема

```
Bootstrap-код:
    channel.bind_writer(payload_a.sensor_out);   // канал инжектирует handle в порт
    channel.bind_reader(payload_b.control_in);   // то же для reader

    registry.add(task_desc_a, wrapper_a);
    registry.add(task_desc_b, wrapper_b);
    registry.add(channel_desc);                  // реестр запоминает топологию

    registry.seal();                             // проверяет: все порты привязаны?
```

Реестр никогда не трогает payload. Привязку делает **канал** (через `bind_writer` / `bind_reader`),
вызываемый bootstrap-кодом напрямую.

## Разделение ответственности

| Кто | Что делает |
|-----|-----------|
| Bootstrap-код | вызывает `channel.bind_writer(port)` / `channel.bind_reader(port)` |
| Канал | инжектирует writer/reader handle в порт; помечает себя как "bound" |
| Реестр | хранит топологию (`ChannelDescriptor`), при `seal()` проверяет полноту привязок |
| Payload | объявляет порты как поля; использует их в `step()` |
| TaskWrapper | не участвует |

## Что меняется в API реестра

`assign_port()` **исчезает** из реестра. Вместо него:

```cpp
// Канал — активный участник привязки:
event_channel.bind_writer(payload_a.sensor_out);
event_channel.bind_reader(payload_b.control_in);

// Реестр — только регистрация дескриптора для валидации:
registry.add(channel_desc);
```

`channel_desc` содержит ссылки на порты (заполненные после `bind_*()`), чтобы `seal()` мог
проверить: тип T совпадает, количество readers соответствует N, нет незаполненных слотов.

## Что меняется в документации

В `STAM_SysRegistry_structure.md` §6.1 заменить:
```cpp
registry.assign_port("TASK_A", "OUT_X", channel, writer);
```
на:
```cpp
channel.bind_writer(task_a_payload.sensor_out);
registry.add(channel_desc);
```

## Вывод

Реестр перестаёт быть "инжектором". Он становится чистым хранилищем топологии
и валидатором при `seal()`. Это корректная архитектура для embedded RT-системы:
простая, без скрытых зависимостей, каждая сущность делает одно дело.

---

[Сессия 0007][Кодекс]

Согласен с опасением оператора: реестр не должен "докапываться до payload" по строкам и рефлексии.
Но полностью убирать `assign_port()` из реестра, на мой взгляд, избыточно и ломает зафиксированные документы v1.

Предлагаю компромисс:
- `assign_port` оставляем как точку привязки в bootstrap;
- убираем строковый поиск поля порта внутри payload;
- bootstrap передаёт в `assign_port` прямую ссылку/указатель на порт.

Пример:
```cpp
registry.assign_port(task_desc, payload_a.sensor_out, ch_temp, writer);
registry.assign_port(task_desc, payload_b.temp_in,   ch_temp, reader);
```

Что это даёт:
- реестр не лезет внутрь payload (порт ему передали явно);
- единая точка истины по топологии и проверкам остаётся в реестре;
- `seal()` остаётся единственным местом финальной структурной валидации;
- минимум breaking changes к текущей фиксированной документации.

Итого по ответственности в этом варианте:
- Bootstrap: выбирает, какой канал к какому порту подключать, и вызывает `assign_port`.
- Реестр: валидирует тип/роль и выполняет bind в конкретный переданный порт.
- Payload/Wrapper: без изменений; wrapper в каналах не участвует.

Моё предложение для Фазы 1: зафиксировать именно этот вариант как базовый для v1.


[Сессия 0008][Оператор]

Но реестр знает, что такое враппер :) Так может делать прокидывание интерфейса канала через враппер?

---

[Сессия 0009][Клод]

Идея понятна и логична: wrapper уже является медиатором между системой (реестр, scheduler)
и payload. Уже прецедент — `attach_hb()` именно так работает: реестр → wrapper → payload.

## Техническая раскладка

Если прокидывать через wrapper:

```
Registry → wrapper.bind_port(name, handle) → payload.port.bind(handle)
```

Для этого wrapper должен:
1. Держать таблицу портов (имя → type-erased указатель на поле payload + функция bind)
2. При вызове `bind_port()` найти порт по имени и вызвать его bind

```cpp
// Что потребуется добавить в TaskWrapper:
struct PortSlot {
    const char*        name;
    void*              port_ptr;            // указатель на поле payload
    void             (*bind_fn)(void*, TypeErasedHandle);
};

PortSlot port_table_[MAX_PORTS];  // static array, no heap
uint8_t  port_count_ = 0;

void bind_port(const char* name, TypeErasedHandle handle) noexcept;
```

А `TaskWrapperRef` (зафиксированная function-table) расширяется пятым указателем:
```cpp
TaskWrapperRef {
    void* obj;
    void (*step_fn) (void*, tick_t) noexcept;
    void (*init_fn) (void*) noexcept;
    void (*alarm_fn)(void*) noexcept;
    void (*done_fn) (void*) noexcept;
    void (*bind_port_fn)(void*, const char*, TypeErasedHandle) noexcept;  // NEW
}
```

## Проблемы

**1. Payload должен заполнить таблицу портов.**
Кто передаёт `{"OUT_X", &payload.sensor_out}` в wrapper? Либо payload реализует новый метод
`register_ports(TaskWrapper&)`, либо разработчик прописывает это вручную при создании wrapper.
В обоих случаях — новый бойлерплейт на стороне разработчика задачи.

**2. Type erasure теряет статические гарантии.**
`attach_hb()` безопасен: один тип, один указатель, нет вариантности.
Для портов тип T и вид канала (Event/State) разные у каждого порта — type-erased handle
либо несёт тип через void* (UB при ошибке), либо требует runtime type tag.

**3. Wrapper получает новую ответственность.**
Сейчас: wrapper = lifecycle adapter (step/init/alarm/done + hb).
После: wrapper = lifecycle adapter + port registry. Это разные обязанности в одном классе.

**4. `TaskWrapperRef` финализирован в v1.**
Добавление `bind_port_fn` — breaking change зафиксированной структуры.

## Сравнение

| | Через wrapper | Bootstrap напрямую (0006 / 0007) |
|--|--|--|
| Реестр трогает payload | нет | нет |
| Тип-безопасность | runtime tag | compile-time |
| Сложность wrapper | растёт | не меняется |
| Бойлерплейт на разработчике | есть (регистрация портов) | минимум |
| Изменения в зафиксированных структурах | TaskWrapperRef | assign_port API |

## Вывод

Прокидывание через wrapper — архитектурно красиво (консистентно с `attach_hb`),
но технически дороже: теряется статическая типизация, wrapper усложняется, payload
требует нового интерфейса.

Ключевой вопрос к оператору: **является ли принципиальным требованием**, чтобы
реестр-wrapper оставался единой точкой входа для всего? Если да — идём через wrapper
и принимаем издержки. Если нет — bootstrap-прямой вызов (0006/0007) проще и чище.


[Сессия 0010][Оператор]
breaking change зафиксированной структуры - Ну если мы продолбались при построении архитектуры, то это не повод тащщить ошибку дальше. 

Может просто требовать от пейлоада наличия метода привязки интерфейса к порту? И прокидывать его через враппер?

---

[Сессия 0011][Кодекс]

Да, это рабочий путь. Поддерживаю идею: требовать у payload явный метод bind и вызывать его через wrapper.

Ключевой момент, чтобы не потерять типобезопасность:
- не делать `bind_port(name, void*)`;
- делать типизированный вызов, где payload сам принимает корректный тип handle.

Схема:
1. Реестр формирует writer/reader handle канала.
2. Реестр вызывает `wrapper.bind_port(port_id, handle)` (или `port_name`, если остаёмся на строках).
3. Wrapper просто делегирует в payload: `payload.bind_port(port_id, handle)`.
4. Внутри payload — `switch(port_id)` и присваивание в конкретный `InPort/OutPort`.

Почему это лучше "толстого wrapper":
- wrapper не хранит таблицу портов и не знает типы портов;
- вся доменная логика портов остаётся в payload;
- реестр не лезет в поля payload напрямую.

Что нужно зафиксировать перед Фазой 2:
- формат идентификатора порта (`enum PortId` предпочтительнее строк);
- контракт payload: обязательный `bind_port(...) noexcept`;
- правило валидации: неизвестный `port_id`/неподходящий handle => ошибка `assign_port/seal`.

Мой голос: если готовы на breaking change, это самый чистый вариант из обсуждённых.

[Сессия 0010][Оператор]

Я точно готов. Пока это чертов пэт-проект. Вылизываем архитектуру.

---

[Сессия 0012][Клод]

Направление верное. Разбираю детали, чтобы не наступить на новые грабли.

## Что делает bind_port в payload

Payload объявляет метод, который принимает идентификатор порта и handle:

```cpp
void bind_port(uint8_t port_id, TypeErasedHandle h) noexcept;
```

Внутри — switch по port_id, payload сам знает, какой тип ожидается:

```cpp
void bind_port(uint8_t port_id, TypeErasedHandle h) noexcept {
    switch (port_id) {
        case PORT_SENSOR_OUT: sensor_out.bind(h.as<EventWriterHandle<SensorData>>()); break;
        case PORT_CTRL_IN:    ctrl_in.bind(h.as<StateReaderHandle<CtrlData>>());      break;
        default:              assert(false);  // неизвестный порт
    }
}
```

`h.as<X>()` — приведение TypeErasedHandle к конкретному типу.
В debug: assert(handle.type_id == TypeId<X>). В release: zero-overhead reinterpret.

## Что такое TypeErasedHandle

Минимальная структура — void* + TypeId:

```cpp
struct TypeErasedHandle {
    void*    ptr;
    uint32_t type_id;   // для debug-валидации

    template<typename H>
    H& as() noexcept {
        assert(type_id == type_id_of<H>());
        return *static_cast<H*>(ptr);
    }
};
```

`type_id_of<H>()` — compile-time хэш типа (constexpr, без RTTI). На embedded реализуется
через `__COUNTER__` или хэш от `__PRETTY_FUNCTION__`.

## Как wrapper делегирует

TaskWrapper уже делегирует init/alarm/done через `if constexpr`. Аналогично:

```cpp
void bind_port(uint8_t port_id, TypeErasedHandle h) noexcept {
    if constexpr (requires(Payload& p) { p.bind_port(port_id, h); }) {
        payload_.bind_port(port_id, h);
    }
    // если payload без портов — молчим
}
```

`TaskWrapperRef` расширяется одним указателем:
```cpp
void (*bind_port_fn)(void* obj, uint8_t port_id, TypeErasedHandle) noexcept;
```

## Проблема: валидация при seal()

Если payload не реализовал `bind_port`, а реестр должен привязать к нему порт — молчаливый
провал. Нужно детектировать на `seal()`.

Решение: канал помечает каждый порт как "bound" при вызове `assign_port()`. `seal()` проходит
по ChannelDescriptor и проверяет:

```
для каждого канала:
    writer_bound == true
    readers_bound == N (для StateChannel)
```

Если `assign_port()` вызван, но `bind_port` у payload не определён — assert в debug при
вызове через TaskWrapperRef, SealError в release.

## Нерешённый вопрос: port_id — чей enum?

Payload определяет свой enum:
```cpp
struct SensorPayload {
    enum : uint8_t { PORT_SENSOR_OUT = 0, PORT_CTRL_IN = 1 };
    ...
};
```

Bootstrap использует эти константы напрямую:
```cpp
registry.assign_port(task_desc_a, SensorPayload::PORT_SENSOR_OUT, channel, writer_side);
```

Реестр видит `uint8_t` — не знает payload-тип. Типобезопасность: статическая (на стороне
bootstrap-кода), не статическая на уровне реестра. Это нормально: реестр — системный уровень,
bootstrap — прикладной уровень.

## Итог: что изменилось от начала обсуждения

| Был (документация v1) | Стал (предлагаемый) |
|---|---|
| `registry.assign_port("TASK", "PORT", ch, side)` | `registry.assign_port(task_desc, port_id, ch, side)` |
| Реестр лезет в payload по имени | Реестр вызывает `wrapper.bind_port_fn()` |
| Wrapper без портов | Wrapper делегирует bind в payload |
| Payload пассивен | Payload реализует `bind_port()` |
| TaskWrapperRef: 4 fn ptrs | TaskWrapperRef: 5 fn ptrs |

## Что нужно зафиксировать перед Фазой 2

1. `TypeErasedHandle` — структура и контракт `as<H>()`
2. `bind_port(uint8_t, TypeErasedHandle) noexcept` — контракт payload
3. `if constexpr` в wrapper (опциональный) или обязательный концепт в tags.hpp?
4. Механизм `type_id_of<H>()` — выбор реализации (constexpr hash без RTTI)
5. Как `seal()` проверяет полноту привязок (`channel.is_fully_bound()`)

**Вопрос к оператору**: `bind_port()` — обязательный метод для всех payload (новый концепт
`HasPorts` в tags.hpp) или опциональный через `if constexpr` (payload без портов молча
пропускает bind)?

[Сессия 0013][Оператор]

## Проблема: валидация при seal()

Если payload не реализовал `bind_port`, а реестр должен привязать к нему порт — молчаливый
провал. Нужно детектировать на `seal()`. Сеал может проверять для всех задач и каналов are_yopu_ready(), а уж канал отдает да/нет, готов/ не готов. Но для этого в интерфейсе нужно иметь метод биндед() видимо. 

Скорее опционально. Могут быть задачи не связанные каналами и поддерживающие только "дыхалку".

---

[Сессия 0014][Кодекс]

Поддерживаю: `bind_port` делаем опциональным.

Предлагаю зафиксировать так:
- payload без каналов: `bind_port` не требуется;
- payload с каналами: обязан поддерживать `bind_port(...)`, иначе `assign_port` возвращает ошибку конфигурации;
- `seal()` проверяет готовность каналов, а не наличие метода у всех payload подряд.

По проверкам:
1. На `assign_port(...)` реестр вызывает `wrapper.bind_port(...)`.
2. Если у payload нет `bind_port`, wrapper возвращает `false` (или `BindResult::unsupported`).
3. Реестр помечает bind как неуспешный и сохраняет причину.
4. На `seal()` реестр делает агрегированную проверку:
   - для каждого канала `is_fully_bound()==true`;
   - нет ошибок bind, накопленных на фазе bootstrap.

Про `are_you_ready()`/`binded()`:
- лучше единый интерфейс канала `is_fully_bound() noexcept -> bool`;
- `binded()` лучше не вводить (нестандартное имя, дублирование смысла).

Итог: задачи "только с дыхалкой" проходят без `bind_port`, канальные задачи валидируются строго.

---

[Сессия 0015][Клод]

Согласен по существу. Уточняю два момента, которые влияют на реализацию.

## 1. bind_port_fn = nullptr — лучше, чем возврат false

Codex предлагает: wrapper возвращает `false`/`BindResult::unsupported`.
Это требует, чтобы wrapper всегда имел функцию — просто возвращающую "нет".

Чище: если payload не имеет `bind_port()`, поле `bind_port_fn` в `TaskWrapperRef` = **nullptr**.

```cpp
TaskWrapperRef {
    void* obj;
    void (*step_fn) (void*, tick_t) noexcept;
    void (*init_fn) (void*) noexcept;
    void (*alarm_fn)(void*) noexcept;
    void (*done_fn) (void*) noexcept;
    void (*bind_port_fn)(void*, uint8_t, TypeErasedHandle) noexcept;  // nullptr если нет портов
}
```

Wrapper строится через `if constexpr`:
```cpp
// При построении TaskWrapperRef из TaskWrapper<Payload>:
.bind_port_fn = requires(Payload& p){ p.bind_port(...); }
    ? [](void* obj, uint8_t id, TypeErasedHandle h) noexcept {
          static_cast<TaskWrapper<Payload>*>(obj)->payload_.bind_port(id, h);
      }
    : nullptr
```

Реестр в `assign_port()`:
```cpp
if (wrapper_ref.bind_port_fn == nullptr) {
    return BindResult::payload_has_no_ports;  // немедленная ошибка
}
wrapper_ref.bind_port_fn(wrapper_ref.obj, port_id, handle);
```

**Плюс**: ошибка ловится немедленно при `assign_port()`, не копится до `seal()`.
**Плюс**: нет лишнего no-op указателя в таблице.

## 2. assign_port() должен возвращать результат

Сейчас в документации `assign_port` возвращает `void`. Нужен `BindResult`:

```cpp
enum class BindResult : uint8_t {
    ok,
    payload_has_no_ports,   // bind_port_fn == nullptr
    unknown_port_id,        // payload вернул ошибку внутри switch
    type_mismatch,          // TypeErasedHandle type_id не совпал
    already_bound,          // порт уже привязан (rebind запрещён)
};

BindResult registry.assign_port(TaskDescriptor&, uint8_t port_id,
                                ChannelDescriptor&, PortSide) noexcept;
```

Bootstrap-код видит ошибку сразу. `seal()` подхватывает то, что bootstrap пропустил
(например, если bootstrap забыл вызвать `assign_port` вообще).

## 3. is_fully_bound() — на канале, это правильно

```cpp
// ChannelDescriptor или сам примитив:
bool is_fully_bound() const noexcept;
// EventChannel: writer_bound && reader_bound
// StateChannel<T,N>: writer_bound && readers_bound_count == N
```

`seal()` итерирует каналы:
```cpp
for (auto& ch : channels_) {
    if (!ch.is_fully_bound()) errors.push("channel not fully bound");
}
```

Это замена `are_you_ready()` оператора — та же идея, правильное имя.

## Итог: схема зафиксирована

```
Bootstrap:
  assign_port(task_a_desc, SensorPayload::PORT_OUT, ch, writer_side)
  → wrapper.bind_port_fn != nullptr ?
      → вызов bind_port_fn → payload.bind_port(PORT_OUT, handle)
          → sensor_out.bind(handle.as<EventWriterHandle<SensorData>>())
          → channel помечает writer как bound
      : BindResult::payload_has_no_ports  ← немедленная ошибка

seal():
  для каждого канала: is_fully_bound() → SealError если нет
```

Задачи без портов: `bind_port_fn == nullptr`, `assign_port` никогда не вызывается для них —
всё тихо, никаких проверок не нужно.

## Что остаётся открытым

Только одно: **TypeErasedHandle и type_id** — как без RTTI получить stable type identity
для debug-assert. Предлагаю вынести в отдельную сессию, как только зафиксируем основную схему.

Предлагаю считать основную схему согласованной и перейти к фиксации в документации.

[Сессия 0016][Оператор]

Именование портов предлагаю сделать 4-х символьным. Остальное пока нравится.

---

[Сессия 0017][Кодекс]

Принято, поддерживаю 4-символьные имена портов.

Предлагаю формальный контракт v1:
- `PortName` = ровно 4 ASCII-символа в верхнем регистре `[A-Z0-9_]`;
- хранение как `uint32_t` (fourcc), чтобы сравнение было O(1) без строк;
- в API можно принимать литерал `"TEMP"` и конвертировать в `PortName`.

Проверки:
1. На `assign_port(...)`: имя порта валидного формата (если пришла строка).
2. На bind в payload: неизвестный `PortName` => `BindResult::unknown_port`.
3. На `seal()`: все каналы `is_fully_bound()==true`.

Нюанс совместимости:
- если у нас уже обсуждён `port_id:uint8_t`, оставляем его внутренним ключом payload;
- `PortName(4)` используем на уровне registry/bootstrap и в отчётах;
- маппинг `PortName -> port_id` хранится в payload (или compile-time таблице).

Если подтверждаете, следующим шагом фиксируем это в документации как правило именования портов v1.

[Сессия 0018][Оператор]

- маппинг `PortName -> port_id` хранится в payload (или compile-time таблице).

хранение в  compile-time таблице представляется разумным.

---

[Сессия 0019][Клод]

Если маппинг хранится в compile-time таблице — предлагаю сделать следующий шаг:
**устранить port_id : uint8_t полностью**. Он не нужен.

## Почему port_id лишний

В схеме Codex: bootstrap → registry → wrapper → payload.bind_port(PortName, handle) →
lookup в таблице → port_id → switch(port_id).

Два уровня идентификации (PortName снаружи + port_id внутри) — это лишняя сложность.
`PortName` уже является числом (`uint32_t` fourcc). Сравнение — одна инструкция.
Его можно использовать как ключ везде — и в API реестра, и внутри payload.

## Предложение: PortName как единственный идентификатор

```cpp
// PortName — constexpr-тип, хранится как uint32_t (fourcc):
struct PortName {
    uint32_t value;
    constexpr explicit PortName(const char (&s)[5]) noexcept
        : value(uint32_t(s[0])<<24 | uint32_t(s[1])<<16 | uint32_t(s[2])<<8 | s[3])
    {}
    constexpr bool operator==(PortName o) const noexcept { return value == o.value; }
};
```

Payload объявляет порты с именами как constexpr-константы:

```cpp
struct SensorPayload {
    static constexpr PortName SOUT{"SOUT"};  // compile-time "table"
    static constexpr PortName CINP{"CINP"};

    EventOutPort<SensorData>  sensor_out;
    StateInPort<CtrlData>     ctrl_in;

    void bind_port(PortName name, TypeErasedHandle h) noexcept {
        if      (name == SOUT) sensor_out.bind(h.as<EventWriterHandle<SensorData>>());
        else if (name == CINP) ctrl_in.bind(h.as<StateReaderHandle<CtrlData>>());
        else    assert(false);
    }
};
```

Bootstrap:
```cpp
registry.assign_port(task_desc_a, SensorPayload::SOUT, sensor_ch, writer_side);
registry.assign_port(task_desc_b, SensorPayload::CINP, sensor_ch, reader_side);
```

## Что убирается

- `port_id : uint8_t` — исчезает. PortName — единственный ключ на всех уровнях.
- Маппинг PortName → port_id — исчезает. "Таблица" = набор `static constexpr PortName` в payload.
- `switch(port_id)` → if-chain по PortName (одна uint32_t-инструкция на сравнение).

## Производительность

`if (name == PortName{"SOUT"})` — два целочисленных сравнения (`uint32_t`).
При constexpr значениях компилятор раскрывает цепочку в минимальный машкод.
Switch vs if-chain на uint32_t-константах — идентичны по скорости.
Это bootstrap-код, не RT — производительность не критична в любом случае.

## Итоговая сигнатура bind_port_fn в TaskWrapperRef

```cpp
TaskWrapperRef {
    void* obj;
    void (*step_fn)     (void*, tick_t)                       noexcept;
    void (*init_fn)     (void*)                               noexcept;
    void (*alarm_fn)    (void*)                               noexcept;
    void (*done_fn)     (void*)                               noexcept;
    void (*bind_port_fn)(void*, PortName, TypeErasedHandle)   noexcept;  // nullptr = нет портов
}
```

## Что нужно зафиксировать в документации

1. `PortName` = 4 ASCII-символа `[A-Z0-9_]`, хранится как `uint32_t` fourcc
2. Payload объявляет `static constexpr PortName` для каждого порта
3. Payload реализует `void bind_port(PortName, TypeErasedHandle) noexcept` — опционально
4. `TaskWrapperRef::bind_port_fn = nullptr` если payload не имеет портов
5. `assign_port(TaskDescriptor&, PortName, ChannelDescriptor&, PortSide) → BindResult`
6. `TypeErasedHandle` — отдельная тема (type_id без RTTI)

Предлагаю это зафиксировать и переходить к написанию документа.

---

[Сессия 0020][Кодекс]

Поддерживаю: `port_id` можно убрать полностью и оставить единый ключ `PortName(4cc)`.

Считаю это правильным упрощением:
- один идентификатор на всех слоях (bootstrap / registry / wrapper / payload);
- исчезает лишний маппинг `PortName -> port_id`;
- меньше шансов на рассинхрон и ошибок конфигурации.

Деталь, которую предлагаю зафиксировать сразу:
- для `bind_port(...)` лучше возвращать статус, а не `assert(false)` на unknown-port.

Например:
```cpp
enum class BindResult : uint8_t {
    ok,
    payload_has_no_ports,
    unknown_port,
    type_mismatch,
    already_bound
};

BindResult bind_port(PortName, TypeErasedHandle) noexcept;
```

Тогда цепочка полностью диагностируема:
- payload возвращает причину;
- wrapper просто транзитно возвращает её в registry;
- registry фиксирует ошибку и возвращает её из `assign_port(...)`;
- `seal()` проверяет полноту по `is_fully_bound()`.

Итого: готов зафиксировать как базовый вариант v1.1
1. `PortName` (4cc) — единственный идентификатор порта.
2. `bind_port_fn` в `TaskWrapperRef` с `PortName`, `nullptr` для задач без портов.
3. `assign_port(..., PortName, ...) -> BindResult`.

[Сессия 0021][Оператор]

Согласен.
Клод, меняй документацию. Отчет - сюда.

---

[Сессия 0022][Клод] — Отчёт об обновлении документации

Внесены изменения в три файла:

## docs/concepts/fixed/STAM_DTO_chanel_structure.md

**§4 Порты** — полная переработка:
- 4 типа портов вместо 2: `EventOutPort<T>`, `EventInPort<T>`, `StateOutPort<T>`, `StateInPort<T>`
- Добавлена §4.2 `PortName` — структура, fourcc, алфавит `[A-Z0-9_]`
- Добавлена §4.3 `BindResult` — enum с 5 кодами
- §4.4 пример payload с `static constexpr PortName` и `bind_port()`
- §4.5 `TypeErasedHandle` — описание (детали type_id — отдельный документ)

**§5.2 ASSIGN** — обновлён:
- Новый API: `registry.assign_port(task_desc, PortName, channel, side) → BindResult`
- Показана цепочка: registry → bind_port_fn → payload.bind_port() → port.bind()

**§5.3 SEAL** — обновлён:
- Добавлен `channel.is_fully_bound()` как механизм проверки seal()

## docs/concepts/fixed/STAM_SysRegistry_structure.md

**§5 Порты** — переработан:
- 4 типа портов с RT-методами
- `PortName` (fourcc) как идентификатор
- `registry.assign_port(task_desc, PortName, channel, side)` как точка привязки

**§6.1 Pre-seal** — обновлён:
- Новый API `assign_port` с `PortName` и возвратом `BindResult`
- Показана внутренняя цепочка вызовов
- Убрана строкова-based `assign_port("TASK_A", "OUT_X", ...)`

**§8 Инварианты портового слоя** — обновлены под новую схему

**§12 Зафиксированные решения** — добавлены:
- `TaskWrapperRef` с 5-м fn ptr `bind_port_fn` (nullable)
- `PortName` контракт
- `BindResult` enum
- `assign_port` сигнатура

## docs/concepts/fixed/STAM_task_wrapper_structure.md

**§4 Интерфейс** — добавлен `bind_port(PortName, TypeErasedHandle) → BindResult noexcept`
с пояснением: метод присутствует только если payload реализует `bind_port()`

**§7 Инварианты** — добавлены два инварианта:
- `bind_port_fn = nullptr` если payload без портов
- `bind_port()` вызывается только в bootstrap, не в RT

## Что остаётся открытым

- `TypeErasedHandle` — детали `type_id_of<H>()` без RTTI (отдельный документ)
- `taskwrapper.hpp` — реализация `bind_port()` через `if constexpr` (Фаза 2)

---

[Сессия 0023][Кодекс]

Проверил отчёт по факту в документах: основные изменения действительно внесены.
Подтверждаю:
- 4 типа портов (`Event/State` × `In/Out`) зафиксированы.
- `PortName` (4cc), `BindResult`, `assign_port(...)->BindResult`, `bind_port_fn` в `TaskWrapperRef` отражены.
- `channel.is_fully_bound()` добавлен как критерий для `seal()`.

Нашёл 1 несоответствие, которое надо поправить:
- `docs/concepts/fixed/STAM_DTO_chanel_structure.md`, §5.1 (DECLARE) ещё содержит старую формулировку
  про `InPort<T> / OutPort<T>`.
  Нужно заменить на новую четвёрку портов (`EventOutPort/EventInPort/StateOutPort/StateInPort`),
  чтобы не было конфликта внутри одного документа.

После этой правки считаю документарную фиксацию Фазы 1 консистентной.

---

[Сессия 0024][Кодекс] — Отчёт по правке

Исправил замечание по несоответствию в документации.

Изменён файл:
- `docs/concepts/fixed/STAM_DTO_chanel_structure.md`

Что поправлено:
- в §5.1 (DECLARE) заменена устаревшая формулировка `InPort<T> / OutPort<T>`;
- теперь явно указаны актуальные типы:
  `EventOutPort<T>`, `EventInPort<T>`, `StateOutPort<T>`, `StateInPort<T>`.

Итог:
- конфликт терминов внутри `STAM_DTO_chanel_structure.md` устранён;
- описание §5.1 приведено в соответствие с §4 и общей согласованной схемой.


[Сессия 0025][Оператор]

Клод, проверяй. Если все ок, переходим к обсуждению - `TypeErasedHandle` — детали `type_id_of<H>()` без RTTI (отдельный документ)
---

[Сессия 0026][Клод] — Финальная проверка документации + переход к TypeErasedHandle

## Результат проверки

Нашёл и исправил 3 оставшихся inconsistency (сверх того, что исправил Codex в 0024):

**1. `STAM_SysRegistry_structure.md` §3 TaskDescriptor:**
- было: `ports : PortList  // типизированные порты (InPort / OutPort)`
- стало: `ports : PortList  // типизированные порты (EventOutPort / EventInPort / StateOutPort / StateInPort)`

**2. `STAM_SysRegistry_structure.md` §4 ChannelDescriptor:**
- было: `writer : PortRef  // ссылка на OutPort задачи-writer`
- было: `readers : PortRef[]  // ссылки на InPort задач-readers`
- стало: `writer : PortRef  // ссылка на EventOutPort / StateOutPort задачи-writer`
- стало: `readers : PortRef[]  // ссылки на EventInPort / StateInPort задач-readers`

**3. `STAM_DTO_chanel_structure.md` §5.3 SEAL — дублирование:**
- Удалены строки, дублирующие условия `is_fully_bound()` в явном виде:
  (они следовали сразу после описания `is_fully_bound()` и повторяли его буквально)
- Оставлена только обобщающая формулировка через `is_fully_bound()`

## Итог

Все три зафиксированных документа приведены в полную консистентность.
Документарная фиксация Фазы 1 завершена.

## Следующий шаг

Готов к обсуждению `TypeErasedHandle` — `type_id_of<H>()` без RTTI (отдельный документ).

[Сессия 0027][Оператор]

Оьсуждаем `TypeErasedHandle` — `type_id_of<H>()` без RTTI (отдельный документ).
Имя файла TypeErasedHandle Contract & Invariants.md

Соображения Клод:

Задача: получить uint32_t type_id для типа H в compile-time, без RTTI (typeid, dynamic_cast — запрещены в RT).

Три подхода:

1. __COUNTER__ / __LINE__-based (non-portable, fragile)


template<typename H>
constexpr uint32_t type_id_of() {
    return __COUNTER__;  // нестабилен между TU, ломается при изменении порядка включения
}
Отбрасываем.

2. Hash от строки __PRETTY_FUNCTION__ / __FUNCSIG__


template<typename H>
constexpr uint32_t type_id_of() noexcept {
    constexpr auto name = std::string_view{__PRETTY_FUNCTION__};
    // djb2 или fnv1a по байтам name
    return fnv1a(name);
}
Плюсы: constexpr, стабилен в рамках одной сборки, нет RTTI.

Минусы: коллизии (маловероятны, но теоретически возможны); имя зависит от компилятора (GCC/Clang __PRETTY_FUNCTION__, MSVC __FUNCSIG__).

3. Explicit registration (zero collision)


// Каждый тип регистрируется явно:
template<> constexpr uint32_t type_id_of<SPSCRing<SensorData,8>>() { return 0x5345'4E44; }
// или: fourcc из имени типа, заданного вручную
Плюсы: полный контроль, zero коллизий, zero runtime.

Минусы: boilerplate — каждый тип регистрируется вручную.

Мой голос: подход 2 (__PRETTY_FUNCTION__ + FNV-1a) — лучший компромисс для RT-embedded:

constexpr, вычисляется при компиляции
Нет ручного boilerplate
Для debug-assert коллизия не критична (crash в debug — ОК)
type_id используется только в debug-сборке для assert, в release вырезается
Вопросы для обсуждения:

Какой hash — FNV-1a или djb2? (оба constexpr, FNV-1a стандартнее)
type_id только в debug (#ifdef NDEBUG) или всегда хранится в TypeErasedHandle?

Конец соображений Клода.

Какой hash — FNV-1a или djb2? (оба constexpr, FNV-1a стандартнее) 
Что дешевле по памяти, процессору?

type_id только в debug (#ifdef NDEBUG) или всегда хранится в TypeErasedHandle?
Не знаю. Ваши соображения.

---

[Сессия 0028][Кодекс]

Мои рекомендации:

1. Хэш: брать **FNV-1a 32-bit**.
- По CPU оба (`djb2`, `FNV-1a`) почти одинаковы: один проход по строке, O(n), только на compile-time.
- По памяти одинаково: храним один `uint32_t`.
- FNV-1a стандартнее и лучше документирован для constexpr-реализаций.

2. `type_id` хранить **только в debug**, в release убрать.
- Назначение `type_id` у нас диагностическое (поймать mismatch в bind), не функциональное.
- В release это лишние 4 байта в каждом `TypeErasedHandle` + лишние операции записи/сравнения.
- Для RT это правильнее: zero-overhead в release, строгие проверки в debug.

Предлагаемый контракт:
- `#ifndef NDEBUG`: `TypeErasedHandle { void* ptr; uint32_t type_id; }`, проверка `type_id` в `bind_into`.
- `#ifdef NDEBUG`: `TypeErasedHandle { void* ptr; }`, проверки `type_id` вырезаны.
- `type_id_of<H>()` реализовать как `constexpr fnv1a_32(__PRETTY_FUNCTION__/__FUNCSIG__)`.

Это даёт хороший баланс: безопасная отладка без RTTI и нулевая цена в release.


[Сессия 0029][Клод] — TypeErasedHandle: согласование + детали реализации

## Согласование с Codex

Полностью согласен с обоими выборами:
- **FNV-1a 32-bit** — принято.
- **`type_id` только в debug** — принято.

## Детали, требующие фиксации

### 1. Что такое `H` в `type_id_of<H>()`?

`TypeErasedHandle` конструируется в `assign_port()` на стороне bootstrap. В этот момент bootstrap
знает конкретный тип канала — например, `EventChannel<SensorData, 8>`. Внутри канала лежит
примитив (`SPSCRing<SensorData,8>` или `SPMCSnapshot<T,N>`). Именно указатель на примитив
(или его часть) и несёт handle.

Таким образом, `H` — это **конкретный тип примитива**:

```
assign_port(task_a, SOUT, sensor_ch, writer_side)
  → TypeErasedHandle{ ptr = &sensor_ch.ring(),
                      type_id = type_id_of<SPSCRing<SensorData,8>>() }
```

В `port.bind(handle)`:
```cpp
// debug:
assert(handle.type_id == type_id_of<SPSCRing<SensorData,8>>());
ptr_ = static_cast<SPSCRing<SensorData,8>*>(handle.ptr);
```

Это означает: коллизия возможна только если два разных примитива дают одинаковый FNV-1a.
Для типов вида `SPSCRing<SensorData,8>` vs `SPSCRing<SensorData,16>` — строки разные,
hash разный. Коллизия практически исключена.

### 2. Кросс-компиляторная обёртка

`__PRETTY_FUNCTION__` (GCC/Clang) и `__FUNCSIG__` (MSVC) дают разные строки, но это не
проблема — хэш вычисляется **в одной сборке**, и главное, что `type_id_of<A>() != type_id_of<B>()`
при `A != B` в рамках одной компиляции.

Нужен макрос-обёртка:
```cpp
#if defined(_MSC_VER)
  #define STAM_FUNC_SIG __FUNCSIG__
#else
  #define STAM_FUNC_SIG __PRETTY_FUNCTION__
#endif
```

### 3. Предлагаемая структура `TypeErasedHandle`

```cpp
namespace stam::model {

#ifndef NDEBUG

constexpr uint32_t fnv1a_32(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

template<typename H>
constexpr uint32_t type_id_of() noexcept {
    return fnv1a_32(STAM_FUNC_SIG);
}

struct TypeErasedHandle {
    void*    ptr;
    uint32_t type_id;

    template<typename H>
    static TypeErasedHandle make(H& obj) noexcept {
        return { &obj, type_id_of<H>() };
    }
};

#else  // NDEBUG (release)

template<typename H>
constexpr uint32_t type_id_of() noexcept { return 0; }

struct TypeErasedHandle {
    void* ptr;

    template<typename H>
    static TypeErasedHandle make(H& obj) noexcept {
        return { &obj };
    }
};

#endif

} // namespace stam::model
```

### 4. Использование в `port.bind(handle)`

```cpp
// EventOutPort<T>::bind(TypeErasedHandle handle) noexcept → BindResult:
#ifndef NDEBUG
    if (handle.type_id != type_id_of<SPSCRing<T,C>>())
        return BindResult::type_mismatch;
#endif
    if (ptr_ != nullptr)
        return BindResult::already_bound;
    ptr_ = static_cast<SPSCRing<T,C>*>(handle.ptr);
    return BindResult::ok;
```

## Вопросы, требующие решения оператора

1. **Где объявляется `TypeErasedHandle`?**
   Предлагаю `stam/model/type_erased_handle.hpp` — рядом с `tags.hpp`.
   Или держать в одном `tags.hpp`?

2. **`type_id_of<H>()` возвращает `0` в release** — это OK?
   Функция существует в обоих режимах, просто trivially zero. Это позволяет писать
   общий `make()` без `#ifdef` на call-site.

3. **StateChannel reader side**: reader получает не указатель на весь `SPMCSnapshot<T,N>`,
   а reader-slot (индекс или ссылку). Тип `H` для reader — `SPMCSnapshot<T,N>` или
   отдельный тип `ReaderSlot`? Нужно определить до фиксации.



   1. **Где объявляется `TypeErasedHandle`?**
   Мне нравится `stam/model/type_erased_handle.hpp` — рядом с `tags.hpp`.

   2. **`type_id_of<H>()` возвращает `0` в release** — это OK?
   Пока не понимаю.

   3. **StateChannel reader side**: reader получает не указатель на весь `SPMCSnapshot<T,N>`,
   а reader-slot (индекс или ссылку). Тип `H` для reader — `SPMCSnapshot<T,N>` или
   отдельный тип `ReaderSlot`? Нужно определить до фиксации.

   Хм. Пока склоняюсь к отдельному типу, но не уверен.

---

[Сессия 0030][Кодекс]

Поддерживаю ваши ответы и предлагаю зафиксировать так:

1. Где объявлять:
- да, отдельный файл `stam/model/type_erased_handle.hpp` — правильный выбор.
- `tags.hpp` оставить только для концептов/базовых тегов.

2. `type_id_of<H>()` в release:
- вариант "в release всегда 0" технически рабочий, но вводит ложное ощущение, что `type_id` полезен в release.
- чище сделать так:
  - `type_id_of<H>()` доступен только в debug (`#ifndef NDEBUG`);
  - в release поле `type_id` и сама проверка отсутствуют полностью.
- тогда API честный: в release это просто `void*` без диагностической семантики.

3. `StateChannel` reader side:
- согласен с вашим уклоном: нужен **отдельный тип handle** для reader-side.
- не `SPMCSnapshot<T,N>`, а что-то вроде `StateReaderSlotHandle<T,N>` (или `SnapshotReaderHandle<T,N>`).
- причина: reader привязывается к конкретному слоту/курсорному состоянию, это отдельная сущность и отдельный контракт.

Рекомендую фиксировать 4 конкретных handle-типа:
- `EventWriterHandle<T,C,P>`
- `EventReaderHandle<T,C,P>`
- `StateWriterHandle<T,N>`
- `StateReaderHandle<T,N>`  (внутри может содержать snapshot ptr + reader_slot)

Так `type_id_of<H>()` сравнивает именно роль+тип, а не "весь канал целиком".


[Сессия 0029][Оператор]
У нас уже есть выделенные интерфейсы для примитивов. Мы строим типы над типами?

Клод, объясни. И поставь секцию [Консенсус]


---

[Сессия 0031][Клод] — Ответ на вопрос оператора + [Консенсус]

## Ответ на вопрос: "Мы строим типы над типами?"

Нет. И именно это правильный ответ на предложение Codex.

Прочитал `spsc_ring.hpp` и `spmc_snapshot.hpp`. Примитивы уже имеют **роль-разделённые типы**:

```
SPSCRingWriter<T,C>    — роль "writer EventChannel"
SPSCRingReader<T,C>    — роль "reader EventChannel"
SPMCSnapshotWriter<T,N> — роль "writer StateChannel"
SPMCSnapshotReader<T,N> — роль "reader StateChannel"
```

Это ровно 4 типа. Они уже существуют. Они уже несут роль в своём имени.

**Следовательно: `H` в `type_id_of<H>()` — это напрямую один из этих 4 типов.**

`TypeErasedHandle::ptr` указывает на объект роль-типа примитива.
Никакие `EventWriterHandle<T>`, `StateReaderHandle<T>` не нужны — это ровно то,
от чего предостерегал оператор.

## Как это работает на практике

`EventChannel<T,C,P>` (и аналогично `StateChannel`) при конструировании создаёт и хранит
роль-объекты внутри себя:

```cpp
// Концептуально — channel владеет примитивом И его роль-объектами
EventChannel {
    SPSCRing<T,C>         ring_;        // владеет Core
    SPSCRingWriter<T,C>   writer_obj_;  // ссылается на ring_.core()
    SPSCRingReader<T,C>   reader_obj_;  // ссылается на ring_.core()
}

StateChannel {
    SPMCSnapshot<T,N>          snapshot_;
    SPMCSnapshotWriter<T,N>    writer_obj_;
    SPMCSnapshotReader<T,N>    reader_objs_[N];  // N pre-allocated reader слотов
}
```

`assign_port()` для writer_side:
```cpp
TypeErasedHandle{ ptr      = &channel.writer_obj_,
                  type_id  = type_id_of<SPSCRingWriter<T,C>>() }
```

`assign_port()` для reader_side (StateChannel — i-й reader):
```cpp
TypeErasedHandle{ ptr      = &channel.reader_objs_[next_reader_idx_],
                  type_id  = type_id_of<SPMCSnapshotReader<T,N>>() }
```

`next_reader_idx_` — счётчик внутри channel, инкрементируется при каждом assign reader.
Лимит N проверяется при bind; превышение → `BindResult::already_bound` (или отдельный `reader_limit_exceeded`).

## Решение вопроса №2 (type_id_of в release)

Codex предложил убрать `type_id_of<H>()` из release полностью.
Уточняю: это правильно, но с оговоркой — функция должна быть **доступна** в обоих режимах,
чтобы `TypeErasedHandle::make<H>()` компилировался без `#ifdef` на call-site.
В release `type_id_of<H>()` просто возвращает 0 и никуда не записывается (поле отсутствует).

Это не "ложная семантика" — это стандартный паттерн для debug-only validation:
функция есть, поле условно. Call-site чист.

---

## [Консенсус]

**Зафиксировано по итогам сессий 0027–0031:**

### 1. Хэш
- **FNV-1a 32-bit** + `__PRETTY_FUNCTION__` / `__FUNCSIG__`
- constexpr, compile-time, без RTTI
- кросс-компиляторный макрос `STAM_FUNC_SIG`

### 2. type_id — debug-only
- `TypeErasedHandle` в debug: `{ void* ptr; uint32_t type_id; }`
- `TypeErasedHandle` в release: `{ void* ptr; }`
- `type_id_of<H>()` существует в обоих режимах; в release возвращает `0` и не хранится

### 3. H = роль-тип примитива (новые типы НЕ создаются)
- 4 типа `H`, все уже существуют:
  - `SPSCRingWriter<T,C>` ← `EventOutPort<T>` хранит pointer на него
  - `SPSCRingReader<T,C>` ← `EventInPort<T>` хранит pointer на него
  - `SPMCSnapshotWriter<T,N>` ← `StateOutPort<T>` хранит pointer на него
  - `SPMCSnapshotReader<T,N>` ← `StateInPort<T>` хранит pointer на него
- Предложение Codex (EventWriterHandle и т.п.) — отклонено

### 4. Владение роль-объектами
- Channel (EventChannel / StateChannel) владеет примитивом И pre-created роль-объектами
- `assign_port()` выдаёт `TypeErasedHandle{ &role_obj, type_id }`
- Для StateChannel: `reader_objs_[N]` — массив, индексируется счётчиком при bind

### 5. Файл
- `stam/model/type_erased_handle.hpp` — отдельный файл рядом с `tags.hpp`

---

[Сессия 0032][Кодекс]

Проверил. В целом `[Консенсус]` корректный, принимаю.

Одно точечное уточнение по `BindResult`:
- для `StateChannel` при попытке привязать reader сверх `N` лучше отдельный код
  `BindResult::reader_limit_exceeded`, а не `already_bound`.

Почему:
- `already_bound` = повторная привязка уже занятого конкретного порта;
- `reader_limit_exceeded` = конфигурационно достигнут лимит readers канала.

Это разные классы ошибок, и их полезно различать в отчёте bootstrap/seal.

Остальное по консенсусу поддерживаю без изменений:
- FNV-1a 32-bit,
- debug-only `type_id`,
- `H` = существующие role-типы примитивов (без новых промежуточных handle-типов),
- отдельный `type_erased_handle.hpp`.

---

[Сессия 0033][Кодекс]

Проверил: новых замечаний после `0032` в ветке обсуждения нет.

Текущий статус считаю согласованным:
- консенсус по `TypeErasedHandle` зафиксирован;
- открытым остаётся только формализация `BindResult::reader_limit_exceeded` в документе/коде.

Готов переходить к следующему шагу (оформление отдельного документа `TypeErasedHandle Contract & Invariants.md` или сразу Фаза 2 в коде).

---

[Сессия 0034][Клод] — reader_limit_exceeded + документ создан

## Согласование с Codex (сессия 0032)

`reader_limit_exceeded` — принято. Codex прав:
- `already_bound` = повторная привязка уже занятого порта задачи
- `reader_limit_exceeded` = канал исчерпал лимит N readers

Разные классы ошибок, разные точки диагностики в bootstrap.

## Документ создан

`docs/concepts/fixed/STAM_TypeErasedHandle_structure.md`

Содержит:
1. Назначение и lifetime
2. Таблица 4 типов H (роль-объекты примитивов)
3. Объявление `TypeErasedHandle` (debug / release ветки), `fnv1a_32`, `type_id_of<H>()`
4. Пример `assign_port()` → `TypeErasedHandle::make()`
5. Пример `port.bind()` с debug-проверкой
6. Полный `BindResult` с `reader_limit_exceeded`
7. Схема владения роль-объектами в EventChannel / StateChannel
8. Инварианты

## Итог Фазы 1 документации

Все документы созданы и синхронизированы:
- `STAM_DTO_chanel_structure.md` — каналы и порты
- `STAM_SysRegistry_structure.md` — реестр
- `STAM_task_wrapper_structure.md` — TaskWrapper
- `STAM_TypeErasedHandle_structure.md` — TypeErasedHandle (новый)

## Следующий шаг

Фаза 2 — реализация в коде. Изменения:

1. `stam/model/type_erased_handle.hpp` — новый файл (TypeErasedHandle, type_id_of, fnv1a_32)
2. `stam/model/tags.hpp` — добавить концепт `HasBindPort` (опциональный bind_port в payload)
3. `stam/exec/tasks/taskwrapper.hpp` — добавить `bind_port()` через `if constexpr`
4. `BindResult` — новый файл или в `tags.hpp`



[Сессия 0034][Оператор]

Кодекс. Отчет сюда.

---

[Сессия 0035][Кодекс] — Отчёт проверки 0034

Проверил фактически по файлам.

Подтверждаю:
- новый документ создан: `docs/concepts/fixed/STAM_TypeErasedHandle_structure.md`;
- в нём есть: `fnv1a_32`, `type_id_of<H>()` (debug/release), `TypeErasedHandle::make()`,
  4 role-типа `H`, полный `BindResult` с `reader_limit_exceeded`, инварианты;
- ссылки/упоминания `TypeErasedHandle` и `bind_port` синхронизированы в:
  - `STAM_DTO_chanel_structure.md`
  - `STAM_SysRegistry_structure.md`
  - `STAM_task_wrapper_structure.md`

Уточнение по статусу Фазы 2:
- в коде (`stam-rt-lib/include/model/tags.hpp`, `stam-rt-lib/include/exec/tasks/taskwrapper.hpp`)
  изменений под новую схему пока нет; это нормально, т.к. текущий отчёт 0034 про документацию.

Итог:
- документарная часть Фазы 1 подтверждена как завершённая;
- можно переходить к реализации Фазы 2 в коде.
