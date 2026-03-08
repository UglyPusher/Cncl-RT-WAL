# PortBinding - PortName and BindResult Contract

## 0. Scope

Defines low-level model types used in channel-to-payload binding:

- `stam::model::PortName`
- `stam::model::BindResult`

## 1. PortName

`PortName` is a 4-byte tag packed into `uint32_t`.

Constructor contract:

- accepts only `const char (&)[5]` (4 visible chars + null terminator)
- computes packed value from first 4 bytes

Purpose:

- stable lightweight port identifier for bootstrap binding

Equality:

- plain value equality on packed `uint32_t`

## 2. BindResult Codes

`enum class BindResult : uint8_t`:

- `ok`
- `payload_has_no_ports`
- `unknown_port`
- `type_mismatch`
- `already_bound`
- `reader_limit_exceeded`

## 3. Producer of BindResult

`BindResult` is produced by payload bind implementation (`payload.bind_port(...)`) and interpreted by `ChannelWrapper`.

Current architecture uses strict bootstrap fail-fast on any non-`ok` result.

## 4. Contract Implication

`BindResult` is not runtime flow-control API.

It is configuration-time status for graph wiring/validation before scheduler start.
