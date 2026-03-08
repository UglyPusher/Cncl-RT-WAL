# TaskWrapperRef - Type-Erased Dispatch Contract

## 0. Scope

`stam::exec::tasks::TaskWrapperRef` is a function-table based type-erased handle over concrete `TaskWrapper<Payload>`.

Fields:

- `void* obj`
- `step_fn`, `init_fn`, `alarm_fn`, `done_fn`
- `is_fully_bound_fn`

## 1. Construction Contract

Use `make_task_wrapper_ref(TaskWrapper<Payload>&)`.

Resulting ref guarantees:

- all function pointers are non-null
- each function pointer casts `obj` back to exact `TaskWrapper<Payload>*`

## 2. Lifetime Contract

`TaskWrapperRef` is non-owning.

Caller must guarantee:

- wrapped `TaskWrapper<Payload>` outlives all uses of the ref
- `obj` points to the same concrete wrapper type used to build function table

Violating these assumptions is undefined behavior.

## 3. Execution Contract

`step_fn(obj, now)` semantics are identical to direct `TaskWrapper::step(now)`:

- payload `step(now)` is executed
- heartbeat is stored with `memory_order_release`

`init_fn/alarm_fn/done_fn` delegate to wrapper methods and preserve wrapper’s compile-time optional-hook behavior.

## 4. Seal Integration

`is_fully_bound_fn(obj)` is consumed by `TaskRegistry::seal(...)`.

Task is treated as unbound if:

- `is_fully_bound_fn == nullptr`, or
- function returns `false`

This maps to `SealResult::Code::task_unbound`.

## 5. Threading and Phase

`TaskWrapperRef` is created in bootstrap and then used by runtime/scheduler glue.

Concurrent calls through one wrapper instance must still obey `TaskWrapper` single-owner/non-reentrant contract.
