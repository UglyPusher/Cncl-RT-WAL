# Bootstrap Lifecycle - End-to-End Contract

## 0. Scope

Defines the canonical `stam-rt-lib` bring-up sequence and phase boundaries.

## 1. Phases

### Phase A: Construction

- construct payloads
- construct `TaskWrapper<Payload>`
- construct `ChannelWrapper<Primitive>`

### Phase B: Bootstrap Wiring

- attach heartbeat pointers (`TaskWrapper::attach_hb`)
- bind channel endpoints into payload ports (`ChannelWrapper::bind_writer/readers`)
- create type-erased refs (`TaskWrapperRef`, `ChannelRef`)
- register tasks in `TaskRegistry`

### Phase C: Seal

- call `TaskRegistry::seal(std::span<const ChannelRef>)`
- require result `SealResult::Code::ok`

### Phase D: Runtime

- start scheduler
- call wrapper `step`/hooks according to scheduling policy
- no further bind/rebind operations

## 2. Hard Rules

- bind/seal happen before runtime start
- any bind violation is treated as configuration error
- system must not enter runtime after failed seal

## 3. Failure Policy

- Channel bind layer is strict fail-fast on non-ok `BindResult`
- Seal layer returns explicit status (`task_unbound`, `channel_unbound`, ...)

Recommended system behavior:

- stop bootstrap and report diagnostics
- do not launch schedulers on invalid graph

## 4. Threading Assumptions

Bootstrap is single-owner deterministic flow for a given graph instance.

Concurrent bootstrap mutation is outside contract.

## 5. Runtime Separation

After successful seal:

- graph topology and bindings are immutable
- runtime path contains no binding logic
- heartbeat and task execution remain deterministic at wrapper level
