# Separated Task Application Model (STAM)

> **Status:** active R&D · contracts-first · License: MIT

STAM is a C++ execution architecture and component model for deterministic real-time systems.
It is **not** an RTOS kernel replacement.

STAM defines how to structure applications from RT and non-RT components connected by explicit channels with formal contracts.

## Why not just FreeRTOS

FreeRTOS provides a scheduler, synchronization primitives, and IPC queues.
It does not define application architecture.

A typical RTOS application grows into this:

```
ISR → queue → task → mutex → shared state → more mutexes → timing chaos
```

Problems: implicit memory model, unbounded jitter, no formal progress guarantees,
RT and non-RT logic mixed in the same tasks.

STAM is an answer to this. It defines:

- **explicit domain separation** — RT and non-RT responsibilities are separated by architecture and enforced by component boundaries
- **semantic channels** — IPC is not a container; it carries a threading model, memory ordering, and progress contract
- **contracts instead of conventions** — happens-before, misuse scenarios, and invariants are specified, not implied

## How to build systems with STAM

1. Decompose the problem into processes, processes into subprocesses
2. Identify **control points** — write an RT controller for each
3. Identify **data sources** (sensors, inputs) — wrap each as a component
4. Connect everything via typed channels
5. Non-RT side (logging, analytics, config) drains from the same channels

Result: a system where every communication path has a defined contract and every domain boundary is explicit.

## What STAM is for

Embedded and industrial control systems where:

- deterministic RT behavior is required
- RT and non-RT responsibilities must be strictly separated
- communication semantics must be explicit and analyzable

## Target platforms

| Architecture | Examples |
|---|---|
| ARM Cortex-M | STM32 and similar |
| ARM Cortex-A | embedded Linux, Raspberry Pi |
| x86/x64 | PC / industrial controllers |

## Execution environments

STAM can run:

- on bare metal
- on top of an RTOS
- on SMP systems

## Core architectural rule

Two domains are separated by design.

**RT domain**
- bounded deterministic execution
- no allocations
- no syscalls
- no blocking
- no locks
- no IO
- explicit invariants and contracts

**non-RT domain**
- persistence
- logging
- analytics
- diagnostics
- configuration
- everything expensive or unpredictable

## Communication model

Channels are typed by semantics, not by implementation.
Each variant exists in a UP (single-core / same-core) and SMP-safe form:

| Semantic | UP | SMP-safe |
|---|---|---|
| Event stream | `SPSCRing` | `SPSCRing` |
| Snapshot publish | `DoubleBuffer`, `Mailbox2Slot` | `DoubleBufferSeqLock`, `Mailbox2SlotSmp` |
| Multi-reader snapshot | `SPMCSnapshot` | `SPMCSnapshotSmp` |

UP variants rely on same-core or preemption-controlled deployment.
SMP variants use seqlock / claim-verify patterns and are safe across cores.

Every channel carries an explicit contract:

- threading model
- memory ordering (happens-before, linearization points)
- progress guarantees (wait-free / bounded completion)
- misuse scenarios and UB model
- RT path budget notes

Contract documentation: [`primitives/docs/`](primitives/docs/)

## Repository structure

```
stam/
├── primitives/          # RT-safe lock-free IPC primitives
│   ├── include/stam/
│   │   ├── primitives/  # SPSCRing, DoubleBuffer, SPMCSnapshot, Mailbox2Slot, CRC32_RT
│   │   └── sys/         # portability layer (arch, compiler, fence, preemption)
│   └── tests/
├── stam-rt-lib/         # RT execution model (TaskWrapper, SysRegistry)
│   └── include/
│       ├── exec/tasks/  # taskwrapper.hpp
│       └── model/       # tags.hpp (concepts: Steppable, RtPayload, ...)
├── modules/             # non-RT infrastructure (logging, persistence, etc.)
├── apps/
│   ├── brewery/         # Reference application: RT control + non-RT logging
│   ├── demo/trivial_tasks/  # Minimal RT/non-RT interaction demo
│   └── minimal/         # Minimal boot example
└── docs/
```

Separation rule: `primitives/` and `stam-rt-lib/` do not depend on `modules/`; reverse dependencies are allowed.

## Architectural layers

**RT primitives** (`primitives/`): lock-free communication primitives with strict RT contracts.
No allocations, no locks, O(1) operations, acquire/release within the C++ memory model.

**Execution model** (`stam-rt-lib/`): RT task/component wiring and scheduler-facing abstractions.
Defines how tasks are registered, scheduled, and observed.

**non-RT modules** (`modules/`): drain RT ring → staging queue / in-memory WAL → sink (file/flash/uart).
Storage reliability policies, analytics, diagnostics, configuration.

## Reference application: Brewery Control

RT loop: temperature sensors → PID/bang-bang control → heater/cooler/pump/valve actuators
→ telemetry snapshot + lossy event log → non-RT.

Safety scenarios covered: overheating, dry-run, sensor failure, watchdog timeout,
escalation FSM (WARN → LIMIT → SHED → PANIC).

non-RT side: logging, persistence, config/calibration, metrics export, trend analysis.

## Safety model

**Safety contract** defines: safe state, fault model, escalation FSM, ownership of safety lines
(no re-entrancy), watchdog strategy (internal + external), shutdown policy.

**Degradation contract** defines: which events are dropped first, which are never dropped
(PANIC/SAFETY), and recovery conditions.

## Implementation principles (non-negotiable)

**RT path:** `noexcept`, no malloc/new/delete, no syscalls/IO/sockets, no locks/waits/condvars.
Errors expressed through return values or counters.

**Portability:** single codebase; configuration via `#define` / optional override header
(`user_sys_config.hpp`). CPU fences and cache maintenance are opt-in and documented.

**Observability:** telemetry via snapshot channels, events via lossy queue (`SPSCRing`),
degradation level as an explicit signal + metrics.

## Roadmap

- [x] Core RT primitives + portability layer
- [ ] RT logger publish API + non-RT drain/sink pipeline
- [ ] Brewery RT domain skeleton (sensors → control → actuators → log)
- [ ] Safety + degradation contracts (docs + code)
- [ ] PC/Linux simulation + unit/property tests
- [ ] Port to STM32

## Getting started

Not yet formalized as a single documented flow.
Interim entry points:

- `apps/minimal` — basic boot
- `apps/demo/trivial_tasks` — minimal RT/non-RT interaction
- `apps/brewery` — full reference scenario
