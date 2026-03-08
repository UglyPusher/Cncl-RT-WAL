# TaskRegistry - Seal Contract & Invariants

## 0. Scope

`stam::exec::TaskRegistry<MaxTasks>` stores task descriptors and validates system readiness at `seal(...)`.

It owns task descriptor storage. Channels are passed as transient `std::span<const stam::model::ChannelRef>`.

## 1. Capacity Contract

- `MaxTasks <= SIGNAL_MASK_WIDTH` (compile-time)
- `add_task(...)` succeeds only in `OPEN` state and while capacity is not exceeded

`SIGNAL_MASK_WIDTH` is derived from `stam::sys::signal_mask_width`.

## 2. Lifecycle States

Registry has two states:

- `OPEN`
- `SEALED`

Rules:

- tasks can be added only in `OPEN`
- `seal(...)` transitions `OPEN -> SEALED` only on success
- second `seal(...)` returns `already_sealed`

## 3. Seal Validation

`seal(channels)` validates both sides:

1. tasks:
- each task must have non-null `is_fully_bound_fn`
- `is_fully_bound_fn(obj)` must return true

2. channels:
- each channel must have non-null `is_fully_bound_fn`
- `is_fully_bound_fn(obj)` must return true

Error mapping:

- unbound task -> `SealResult::Code::task_unbound`, `failed_name = task_name`
- unbound channel -> `SealResult::Code::channel_unbound`, `failed_name = channel name`

## 4. Explicit Channel Span Requirement

`seal(...)` requires explicit channel span argument.

This prevents accidental "seal without channel validation" calls.

For systems without channels, pass explicit empty span:

- `std::span<const stam::model::ChannelRef>{}`

## 5. Integration Contract

Expected bootstrap order:

1. create tasks/channels
2. bind ports
3. add tasks to registry
4. call `seal(channel_refs)`
5. start scheduler only if seal result is `ok`

## 6. Threading Assumption

Registry mutation and sealing are bootstrap operations.
Concurrent mutation/seal is outside contract.
