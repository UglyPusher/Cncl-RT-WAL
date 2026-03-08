# ChannelWrapper - Bootstrap Contract & Invariants

## 0. Scope

`stam::model::ChannelWrapper<Primitive>` is a bootstrap-time adapter that binds primitive writer/reader handles into payload ports.

It is not a runtime transport scheduler component.

## 1. Primitive Contract

`Primitive` must satisfy:

- `writer()` exists
- `reader()` exists

Reader capacity is detected as:

1. `Primitive::max_readers` (value), else
2. `Primitive::max_readers()` (function), else
3. fallback `1`

`ChannelWrapper::max_readers > 0` is compile-time enforced.

## 2. Internal State

Wrapper tracks:

- primitive instance
- single cached writer handle (`writer_obj_`) created in constructor
- `writer_bound_`
- `next_reader_idx_`

`is_fully_bound()` is true only when:

- writer is successfully bound
- number of successful reader binds equals `max_readers`

## 3. Bind API

### `bind_writer(payload, PortName)`

- first successful bind marks writer as bound
- repeated writer bind is contract violation (fail-fast)

### `bind_reader(payload, PortName)`

- bind allowed while `next_reader_idx_ < max_readers`
- successful bind increments `next_reader_idx_`
- exceeding reader limit is contract violation (fail-fast)

## 4. Bind Result and Failure Policy

`payload.bind_port(...)` returns `stam::model::BindResult`.

Current wrapper policy is strict fail-fast for any non-`ok` result, including:

- `payload_has_no_ports`
- `unknown_port`
- `type_mismatch`
- `already_bound`
- `reader_limit_exceeded`

So configuration errors abort during bootstrap instead of leaking into runtime.

## 5. Important Behavioral Note

`bind_reader()` currently acquires primitive reader handle before `payload.bind_port(...)` result is known.

In strict bootstrap contract this is acceptable because any bind failure is fatal immediately.

## 6. Intended Usage Phase

`ChannelWrapper` methods are intended for bootstrap wiring only:

- declare graph
- bind writer/reader ports
- validate with `is_fully_bound()` / `TaskRegistry::seal(...)`
- start runtime only after successful seal

Any bind attempt during runtime is outside contract.
