# Separated Task Application Model (STAM)

> **Status:** active R&D · contracts-first · License: MIT

A C++ library of RT and non-RT components for embedded/industrial systems, built around strict domain separation and explicit contracts. Includes a *brewery control* reference application as a real-world demonstration.

**Core principle:**
- **RT domain** — bounded, deterministic, no-alloc, no syscalls, no blocking.
- **non-RT domain** — persistence, analytics, diagnostics; everything expensive and potentially unpredictable.

---

## Target platforms

| Architecture | Examples |
|---|---|
| ARM Cortex-M | STM32, etc. |
| ARM Cortex-A | embedded Linux, Raspberry Pi |
| x86/x64 | PC / industrial controllers |

---

## Repository structure

```
stam/
├── primitives/          # RT-safe lock-free primitives (SPSCRing, DoubleBuffer, …)
│   ├── include/stam/
│   │   ├── primitives/  # SPSCRing, DoubleBuffer, SPMCSnapshot, Mailbox2Slot, CRC32_RT
│   │   └── sys/         # portability layer (arch, compiler, fence, preemption)
│   └── tests/
├── stam-rt-lib/         # RT execution model (TaskWrapper, SysRegistry)
│   └── include/
│       ├── exec/tasks/  # taskwrapper.hpp
│       └── model/       # tags.hpp (concepts: Steppable, RtPayload, …)
├── modules/             # Non-RT components (logging, …)
├── apps/
│   ├── brewery/         # Reference application: RT control + non-RT logging
│   ├── demo/
│   │   └── trivial_tasks/ # Minimal RT/non-RT interaction demo
│   └── minimal/         # Minimal boot example
└── docs/
```

**Hard rule:** `primitives/` and `stam-rt-lib/` do not depend on `modules/`. The reverse is allowed.

---

## Architecture

### RT Components (hard-RT path)
- no allocations, no syscalls/IO, no locks/waits
- bounded O(1) operations
- acquire/release strictly per C++ memory model
- explicit invariants and misuse contracts per component

Primitives: `SPSCRing`, `DoubleBuffer`, `SPMCSnapshot`, `Mailbox2Slot`, `CRC32_RT`

### Non-RT Components
- drain RT-ring → staging queue / in-memory WAL → sink (file/flash/uart)
- storage durability policies (fsync/flush, batching, backpressure)
- analytics / diagnostics / UI / environment simulators

### HAL (Hardware Abstraction Layer)
Isolates platform-specific code: tick source, GPIO/ADC/SPI/I2C/UART, cache maintenance, watchdog, failsafe outputs. RT code depends on HAL through a minimal interface only.

---

## Reference application: Brewery Control

**RT loop:** temperature sensors → PID/bang-bang control → heater/cooler/pump/valve actuators → telemetry snapshot + lossy event log → non-RT.

**Safety scenarios covered:** overheat, dry-run, sensor failure, watchdog timeout, escalation FSM (WARN → LIMIT → SHED → PANIC).

**non-RT:** logging, persistence, config/calibration, metrics export, trend analysis.

---

## Component contracts

Every primitive and component carries a formal contract covering:

- Scope / semantic model
- Compile-time and runtime invariants
- Threading model preconditions
- Memory ordering (happens-before), linearization points
- Progress guarantees (wait-free / bounded completion)
- Misuse scenarios and UB model
- RT path budget notes

Contract docs: [`primitives/docs/`](primitives/docs/)

---

## Safety model

**Safety contract** defines: safe state, fault model, escalation FSM, ownership of safety lines (no re-entrancy), watchdog strategy (internal + external), shutdown policy.

**Degradation contract** defines: which events are dropped first, which are *never* dropped (PANIC/SAFETY), and recovery conditions.

---

## Implementation principles (non-negotiable)

**RT path:** `noexcept`, no malloc/new/delete, no syscalls/IO/sockets, no locks/waits/condvars. Failures expressed as return values or counters.

**Portability:** single codebase; configuration via `#define` / optional override header (`user_sys_config.hpp`). CPU fences and cache maintenance are opt-in and documented.

**Observability:** telemetry via snapshot (`DoubleBuffer`), events via lossy queue (`SPSCRing`), degradation level as an explicit signal + metrics.

---

## Roadmap

- [x] Core RT primitives + portability layer
- [ ] RT logger publish API + non-RT drain/sink pipeline
- [ ] Brewery RT domain skeleton (sensors → control → actuators → log)
- [ ] Safety + degradation contracts (docs + code)
- [ ] PC/Linux simulation + unit/property tests
- [ ] Port to STM32

---

## Quick start

Not yet available as a single documented flow.  
Interim entry points are:
- `apps/minimal` for bootstrapping
- `apps/demo/trivial_tasks` for RT/non-RT interaction
- `apps/brewery` for the reference scenario
