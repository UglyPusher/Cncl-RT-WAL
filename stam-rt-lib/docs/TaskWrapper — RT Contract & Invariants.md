# TaskWrapper - RT Contract & Invariants

## 0. Scope

`stam::exec::tasks::TaskWrapper<Payload>` is a thin runtime adapter between scheduler and payload.

Responsibilities:

- call `payload.step(now)`
- publish completion heartbeat (`hb_->store(now, release)`)
- delegate optional hooks `init()/alarm()/done()`

Non-responsibilities:

- no channel binding logic
- no scheduling policy
- no watchdog/deadline logic

## 1. Compile-time Contract

`Payload` must satisfy `stam::model::Steppable<Payload>`:

- `void step(stam::model::tick_t now) noexcept`

Heartbeat type contract:

- `stam::model::heartbeat_word_t` is lock-free atomic (enforced by `static_assert` in `model/tags.hpp`)

## 2. Runtime Invariants

For one `TaskWrapper` instance:

- exactly one `attach_hb()` call is allowed
- `attach_hb(nullptr)` is forbidden
- `step()` requires already attached heartbeat
- wrapper is non-copyable and non-movable

Current implementation enforces these with `assert`.

## 3. Execution Semantics

`step(now)`:

1. assert `hb_ != nullptr`
2. call `payload_.step(now)`
3. `hb_->store(now, std::memory_order_release)`

Interpretation:

- heartbeat value is the last successfully completed tick
- completion linearization point is heartbeat store

## 4. Optional Hooks

Wrapper delegates optional payload hooks when present:

- `init()`
- `alarm()`
- `done()`

Dispatch is compile-time (`if constexpr`), with zero runtime virtual overhead.

## 5. Binding Visibility

`is_fully_bound()` behavior:

- if payload has `bool is_fully_bound() const noexcept`, wrapper delegates to it
- otherwise wrapper returns `true`

This allows `TaskRegistry::seal(...)` to validate task readiness without requiring every payload to implement port-binding API.

## 6. Threading Model

Contract assumption:

- wrapper lifecycle methods are called by system bootstrap/scheduler in a controlled single-owner model
- concurrent/reentrant calls on the same wrapper instance are outside contract

## 7. Bootstrap vs Runtime Boundary

Intended phase split:

- bootstrap: object construction, `attach_hb()`, optional `init()`, channel/port binding, `seal()`
- runtime: `step()` and optional `alarm()/done()` according to scheduler logic

Binding is intentionally outside `TaskWrapper`, by design.

## 8. Failure Policy

Current policy is fail-fast in debug/assert-enabled builds:

- invalid heartbeat attach or duplicate attach -> assertion failure
- calling `step()` before `attach_hb()` -> assertion failure

Production policy can layer additional diagnostic handling in system glue code, but wrapper itself remains minimal.
