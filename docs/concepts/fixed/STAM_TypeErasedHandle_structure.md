# TypeErasedHandle — Структура и контракты

*STAM System Registry v1*

---

## 1. Назначение

`TypeErasedHandle` — type-erased указатель на роль-объект примитива канала.
Используется в `assign_port()` для передачи handle от реестра через `TaskWrapperRef` в `payload.bind_port()`.

Несёт:
- `void* ptr` — указатель на конкретный роль-объект примитива
- `uint32_t type_id` — compile-time идентификатор типа (только в debug)

`TypeErasedHandle` **не владеет** объектом, на который указывает. Lifetime объекта — lifetime канала.

---

## 2. Типы H — роль-объекты примитивов

`H` — всегда один из 4 существующих роль-типов примитивов:

| H | Канал | Направление | Хранится в |
|---|-------|-------------|------------|
| `SPSCRingWriter<T,C>` | `EventChannel<T,C,P>` | writer | `EventOutPort<T>` |
| `SPSCRingReader<T,C>` | `EventChannel<T,C,P>` | reader | `EventInPort<T>` |
| `SPMCSnapshotWriter<T,N>` | `StateChannel<T,N>` | writer | `StateOutPort<T>` |
| `SPMCSnapshotReader<T,N>` | `StateChannel<T,N>` | reader | `StateInPort<T>` |

Новые типы-обёртки не создаются. `H` уже несёт роль в своём имени.

---

## 3. Объявление TypeErasedHandle

```cpp
// stam/model/type_erased_handle.hpp

#include "stam/sys/sys_compiler.hpp"  // STAM_FUNC_SIG

namespace stam::model {

// ---------------------------------------------------------------------------
// Кросс-компиляторный источник имени типа для FNV-1a
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
  #define STAM_FUNC_SIG __FUNCSIG__
#else
  #define STAM_FUNC_SIG __PRETTY_FUNCTION__
#endif

// ---------------------------------------------------------------------------
// FNV-1a 32-bit — compile-time hash строки
// ---------------------------------------------------------------------------
constexpr uint32_t fnv1a_32(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h;
}

// ---------------------------------------------------------------------------
// type_id_of<H>() — compile-time идентификатор типа, без RTTI
// ---------------------------------------------------------------------------
// В debug: хэш имени функции-специализации, уникален для каждого H.
// В release: возвращает 0 (не используется, поле отсутствует).
// Функция существует в обоих режимах — call-site не требует #ifdef.

#ifndef NDEBUG

template<typename H>
constexpr uint32_t type_id_of() noexcept {
    return fnv1a_32(STAM_FUNC_SIG);
}

#else

template<typename H>
constexpr uint32_t type_id_of() noexcept { return 0; }

#endif

// ---------------------------------------------------------------------------
// TypeErasedHandle
// ---------------------------------------------------------------------------

#ifndef NDEBUG

struct TypeErasedHandle {
    void*    ptr;
    uint32_t type_id;

    template<typename H>
    static TypeErasedHandle make(H& obj) noexcept {
        return { &obj, type_id_of<H>() };
    }
};

#else  // release

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

---

## 4. Использование в assign_port()

Bootstrap создаёт `TypeErasedHandle` из роль-объекта канала:

```cpp
// EventChannel writer:
TypeErasedHandle::make(event_ch.writer_obj_)
  // debug:   { ptr=&writer_obj_,  type_id=type_id_of<SPSCRingWriter<T,C>>()  }
  // release: { ptr=&writer_obj_ }

// StateChannel reader (i-й):
TypeErasedHandle::make(state_ch.reader_objs_[i])
  // debug:   { ptr=&reader_objs_[i], type_id=type_id_of<SPMCSnapshotReader<T,N>>() }
  // release: { ptr=&reader_objs_[i] }
```

---

## 5. Использование в port.bind()

Порт извлекает указатель после debug-проверки типа:

```cpp
// EventOutPort<T>::bind(TypeErasedHandle h) noexcept → BindResult:
#ifndef NDEBUG
    if (h.type_id != type_id_of<SPSCRingWriter<T,C>>())
        return BindResult::type_mismatch;
#endif
    if (ptr_ != nullptr)
        return BindResult::already_bound;
    ptr_ = static_cast<SPSCRingWriter<T,C>*>(h.ptr);
    return BindResult::ok;
```

Аналогично для остальных трёх типов портов.

---

## 6. BindResult — полный enum

```cpp
enum class BindResult : uint8_t {
    ok,
    payload_has_no_ports,   // bind_port_fn в TaskWrapperRef == nullptr
    unknown_port,           // PortName не распознан payload-ом
    type_mismatch,          // неверный type_id (debug-assert)
    already_bound,          // повторный bind конкретного порта — запрещён
    reader_limit_exceeded,  // попытка привязать reader сверх N у StateChannel
};
```

Разграничение `already_bound` / `reader_limit_exceeded`:
- `already_bound` — конкретный порт уже занят (ошибка конфигурации задачи)
- `reader_limit_exceeded` — канал исчерпал лимит readers (ошибка конфигурации канала)

---

## 7. Владение роль-объектами

Channel владеет примитивом и pre-created роль-объектами:

```
EventChannel<T,C,P> {
    SPSCRing<T,C>        ring_        // владеет Core
    SPSCRingWriter<T,C>  writer_obj_  // ссылается на ring_.core(), создан при init
    SPSCRingReader<T,C>  reader_obj_  // ссылается на ring_.core(), создан при init
}

StateChannel<T,N> {
    SPMCSnapshot<T,N>          snapshot_           // владеет Core
    SPMCSnapshotWriter<T,N>    writer_obj_          // ссылается на snapshot_.core()
    SPMCSnapshotReader<T,N>    reader_objs_[N]      // N pre-allocated reader слотов
    uint8_t                    next_reader_idx_ = 0 // счётчик выданных reader-handle
}
```

`TypeErasedHandle::ptr` действителен на всё время жизни channel-объекта.
После `seal()` все ссылки стабильны — lifetime гарантируется реестром.

---

## 8. Инварианты

- `TypeErasedHandle` не владеет объектом. Не копирует данные.
- `make<H>()` — единственный способ создания. Прямая конструкция запрещена.
- `type_mismatch` — debug-only ошибка; в release проверка вырезана (`static_cast` без проверки).
- `type_id_of<H>()` стабилен в рамках одной сборки. Между сборками не сохраняется.
- Коллизия FNV-1a: теоретически возможна, но для типов вида `SPSCRingWriter<T,C>` при разных T/C практически исключена. В debug — crash при mismatch ≡ ошибка конфигурации.
- `reader_limit_exceeded` проверяется в channel при выдаче `reader_objs_[i]`: если `next_reader_idx_ >= N` — возвращается без выдачи handle.

---

*STAM TypeErasedHandle v1 — Structure & Contracts*
*Составлено по итогам сессий 0027–0034*
