# Design: Real-Time Logging System Based on WAL Principles

## Purpose

This document describes the design of a logging / journaling subsystem intended for use in real-time execution contexts, drawing structurally on principles commonly associated with Write-Ahead Logging (WAL).

The document focuses on architecture, separation of concerns, and design rationale.  
It does not redefine the task, introduce requirements, or specify acceptance criteria beyond what is stated in `task.md`.

---

## Design Intent

The primary intent of this design is to:

- Preserve deterministic behavior in real-time (RT) execution paths
- Apply WAL-inspired structural concepts to logging without assuming storage-level durability in RT contexts
- Clearly separate RT-critical and non-RT responsibilities
- Support crash-consistent inspection and post-mortem analysis
- Remain adaptable to different RT platforms and deployment policies

---

## Architectural Overview

The system is structured around a strict separation of domains:

**RT Logging Domain**  
Responsible for log record emission, not persistence.

**Non-RT Persistence Domain**  
Responsible for log consumption and storage management.

The interaction between these domains is explicit, minimal, and unidirectional.

---

## RT Logging Domain

The RT domain is responsible for log record emission, not persistence.

**Characteristics:**
- Log emission executes with bounded and predictable behavior
- No unbounded blocking is introduced in RT execution paths
- No filesystem interaction or persistence guarantees are assumed
- Memory usage is bounded and determined outside the RT path

From the perspective of RT code, logging is treated as event publication.

---

## Non-RT Persistence Domain

The non-RT domain is responsible for log consumption and storage management.

**Characteristics:**
- Operates asynchronously with respect to RT execution
- Performs serialization, batching, and storage IO
- Applies durability, rotation, and retention policies
- May lag behind RT producers without affecting RT determinism

Persistence progress is decoupled from RT execution timing.

---

## Phase Separation: INIT and RUN

The system operates in two strictly separated phases:

### INIT (non-RT)
- Configuration parsing and validation
- Backend selection and initialization
- File/device opening
- Memory preallocation
- Queue and thread setup

### RUN (steady state)
- RT components active
- No strings, no allocation, no syscalls in RT paths
- Configuration objects destroyed

**Invariant:**  
No RT operation is allowed before INIT completes successfully.

A one-way barrier separates these phases. RT behavior before readiness is well-defined (e.g., publish returns InvalidState).

---

## WAL-Inspired Structural Model

The design borrows from WAL principles in a structural and organizational sense, rather than as a transactional or durability contract.

### Append-Only Record Flow
- Log records are emitted sequentially
- Previously emitted records are not modified in place
- Ordering is preserved within each logical stream

### Logical Boundaries
- Records may be grouped into logical segments or epochs
- Boundaries are explicitly represented and advisory in nature
- Such boundaries aid inspection, recovery, and reasoning about state

### Crash Consistency
- Records are self-describing and length-delimited
- Incomplete or torn records can be detected during replay
- Recovery proceeds until the last verifiable record boundary

Durability at emission time is not assumed.

---

## Functional Decomposition

The design implements a dual-stream approach:

**Critical Stream** — correctness and safety-relevant events  
**Normal Stream** — diagnostics and telemetry

This separation allows different overflow semantics and durability policies for critical vs non-critical events. The separation may be realized through independent buffers, queues, or other mechanisms as appropriate.

---

## Data Flow

A typical flow proceeds as follows:

1. RT code emits a log record into a bounded, pre-established buffer
2. The record becomes visible to a non-RT consumer via explicit publication
3. The non-RT domain routes the record according to stream type
4. Backends serialize and persist records according to policy
5. Storage state advances independently of RT execution

RT execution does not wait on persistence progress.

---

## Concurrency Model

**RT producers do not block on non-RT components.**

Publication points explicitly define record visibility. Single-producer or multi-producer arrangements may be supported, depending on deployment context.

Memory visibility is enforced via explicit publication semantics. The specific synchronization mechanisms (e.g., acquire-release memory ordering) are considered an implementation detail and are not mandated by this design.

---

## Failure Modes and Recovery Considerations

### RT Buffer Pressure

The design allows for policy-driven handling of buffer pressure:

**Critical stream overflow:**
- Treated as a system-critical condition
- May trigger fault escalation
- RT execution continues (no blocking or panic)
- Escalation is handled by non-RT supervisor

**Normal stream overflow:**
- May drop, overwrite, sample, or disable
- Degradation is allowed
- RT execution continues unaffected

The chosen policy does not affect RT determinism.

---

### Non-RT Consumer Interruption

- RT logging continues until buffer limits are reached
- Persisted data remains recoverable up to the last completed write
- Consumers may resume from the last verifiable position after restart

---

### System Crash or Power Loss

**In-memory state:**
- Buffer contents may be lost
- Partial records may exist in non-RT queues

**Persisted state:**
- Logs are scanned sequentially during recovery
- Invalid or incomplete records are discarded
- RT domain state reconstruction is independent of persistence state

**Backend Requirements:**
- Must detect truncated records
- Must provide record boundaries
- Implementation method (CRC/length prefix/magic) is backend-specific

---

## State Management

The system maintains operational state to track degradation and fault conditions.

**State transitions:**
- Normal operation
- Degraded (normal stream degraded)
- Critical fault (critical stream degraded)
- Safe mode (post-fault state)

State transitions are performed by a non-RT supervisor component. RT components may signal fault conditions but do not perform state transitions themselves.

---

## Capacity Model

The design requires pre-sizing of buffers and queues to ensure RT determinism.

**Key parameters:**
- Maximum number of RT producers
- Maximum record size
- Buffer depths (per stream)
- Dispatch budgets (records processed per tick)
- Backend queue depths

**Invariants:**
- All buffers and queues are fully preallocated
- Steady-state production rate must not exceed dispatch capacity
- Buffer depth must accommodate worst-case dispatch latency

The specific values for these parameters are deployment-specific and outside the scope of this design.

---

## Configuration and Policy Separation

The design intentionally separates structural design from operational policy.

Examples of policy-controlled aspects:
- Buffer sizing
- Overrun handling strategy
- Persistence cadence (sync frequency, batching)
- Log rotation and retention
- Backend selection (file, direct IO, raw device)

Policies are applied outside the RT execution path and may include "paranoid mode" for stricter durability at the expense of backend performance (not RT performance).

---

## Observability and Diagnostics

Observability facilities are confined to the non-RT domain and may include:
- Dropped record counters
- Consumer lag indicators
- Persistence error reporting
- State transition events

RT components remain minimal and avoid diagnostic side effects by design. Metrics may be collected via atomic counters for informational purposes, but are not used for synchronization or control flow in RT paths.

---

## Design Trade-offs

**Strict RT determinism precludes synchronous persistence.**  
Immediate durability is intentionally traded for predictability. RT logging provides event publication, not durable commit.

**Decoupling emission from storage improves robustness under load.**  
Backend stalls (disk full, filesystem hang) do not propagate into the RT domain. The system remains operational even when persistence is degraded.

**WAL-inspired structure aids reasoning about ordering and recovery.**  
Append-only semantics and logical boundaries provide a proven mental model for inspectable, recoverable logs.

**Dual-stream separation allows critical events to receive stronger guarantees.**  
Loss of diagnostic telemetry is acceptable; loss of safety-critical events triggers fault escalation.

These trade-offs align with the stated intent of the design.

---

## Non-Goals

This design does not:
- Mandate a specific RTOS, kernel, or scheduling model
- Define durability service levels or SLAs
- Prescribe particular synchronization primitives (beyond publication semantics)
- Attempt to unify logging with transactional state management
- Provide hard-RT durability on commodity storage (SSD/HDD)
- Support dynamic reconfiguration during RUN phase

---

## Platform Note

This implementation targets general-purpose Linux kernel and therefore does not claim system-wide hard real-time guarantees.

The design and invariants are RT-correct by construction and are intended to be portable to RTOS or PREEMPT_RT environments without architectural changes.

Actual worst-case execution time (WCET) analysis and timing bounds are platform-specific and outside the scope of this architectural document.

---

## Relationship to Task Statement

This design:
- Implements the task as stated in `task.md`
- Does not extend or reinterpret the formal task scope
- Makes explicit only those assumptions required to explain design choices

Any changes to scope or requirements must be reflected in `task.md`.

---

## Status

Design brief intended for review and implementation.

---

---

# Appendix: Implementation Considerations

> **Note:** This section is informational and does not form part of the formal design.  
> It provides guidance for implementation but does not mandate specific approaches.

---

## Suggested Component Structure

An implementation may choose to realize the design through components such as:

**RtWriter** — RT-side publication interface  
**RtDispatcher** — RT-side routing with bounded budgets  
**Non-RT Supervisor** — State management and escalation  
**FileBackend / DummyBackend** — Persistence implementations

These names are illustrative. Alternative decompositions are valid.

---

## Memory Ordering Considerations

If using C++ atomics or similar, publication semantics may be realized through:
- `memory_order_release` on write
- `memory_order_acquire` on read
- `memory_order_relaxed` for informational counters

The INIT→RUN barrier may use acquire-release semantics to prevent reordering.

---

## Sequence Numbering

Per-producer monotonic sequence numbers (e.g., 64-bit unsigned) aid in detecting loss and reordering during post-mortem analysis. Wraparound is typically negligible for practical deployments (584 years @ 1 GHz).

---

## Testability

A DummyBackend implementation is recommended for testing. It should support:
- Configurable latency injection
- Deterministic fault injection (write errors, fsync failures)
- Tick-by-tick deterministic execution

Test hooks at key injection points (publish, enqueue, backend write) enable comprehensive fault scenario testing.

---

## Capacity Sizing Example

For illustration (deployment-specific):

| Parameter | Example Value |
|-----------|---------------|
| Max producers | 4 |
| Max record size | 256 bytes |
| Critical buffer depth | 1024 records |
| Normal buffer depth | 4096 records |
| Dispatcher budget | 64 records/tick |
| Tick frequency | 1000 Hz |

These values would need verification against actual workload and platform constraints.

---

## FSM Example

An implementation might realize state management through a finite state machine:

```
INIT → NORMAL → DEGRADED → CRITICAL → SAFE
```

With transitions driven by:
- INIT completion
- Stream degradation events
- Explicit safe-mode entry

This is one valid approach; others are possible.

---

## Build and Test Structure

Suggested repository structure:

```
src/           # Implementation
tests/         # Unit, integration, fault injection tests
examples/      # Minimal usage examples
docs/          # Additional documentation
```

CMake or similar build system recommended for portability.

---

**End of Appendix**
