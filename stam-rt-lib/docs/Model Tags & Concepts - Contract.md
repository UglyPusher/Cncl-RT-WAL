# Model Tags and Concepts - Contract

## 0. Scope

`stam-rt-lib/include/model/tags.hpp` defines base timing types and compile-time concepts used by task/runtime glue.

## 1. Core Types

- `heartbeat_word_t = uint32_t`
- `tick_t = heartbeat_word_t`

Compile-time requirement:

- `std::atomic<heartbeat_word_t>::is_always_lock_free == true`

Rationale:

- deterministic heartbeat publication without hidden locks

## 2. RT Classification Tags

- `rt_safe_tag`
- `rt_unsafe_tag` (compatibility marker)

`RtSafe<T>` accepts only `T::rt_class == rt_safe_tag`.

## 3. Behavioral Concepts

### `Steppable<T>`

Requires:

- `t.step(now)` exists
- `noexcept`
- returns `void`

### `RtHooks<T>`

Optional hooks are allowed, but if present must be `noexcept`:

- `init()`
- `alarm()`
- `done()`

### `RtPayload<T>`

Composite:

- `RtSafe<T> && Steppable<T> && RtHooks<T>`

## 4. Integration Notes

- `TaskWrapper` currently requires `Steppable`.
- `RtPayload` is available for stricter compile-time gates in higher-level APIs.

## 5. Contract Intent

All concept constraints are designed to fail at compile-time, not at runtime.

This keeps RT call paths minimal and avoids dynamic capability checks.
