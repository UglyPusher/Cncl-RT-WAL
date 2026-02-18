# Layering Model

This document defines the architectural layering model of the RT framework.

The layering model is a **design constraint**, not a cosmetic organization rule.
It exists to preserve:

- Real-time determinism
- Portability
- Replaceability of subsystems
- Safety isolation
- Long-term maintainability

---

## 1. Motivation

Real-time systems fail when:

- Non-RT behavior contaminates RT code paths
- Platform-specific details leak into logic
- Logging or diagnostics block execution
- Cross-layer dependencies accumulate

Strict layering is used to prevent these failure modes.

The framework enforces separation between:

- Execution model
- Data transport
- Real-time logic
- Non-real-time infrastructure
- Hardware abstraction
- Application logic

Each layer has a single responsibility.

---

## 2. Architectural Principles

### 2.1 RT Isolation

The RT path must be:

- Bounded in execution time
- Free of dynamic allocation
- Free of syscalls
- Free of blocking synchronization
- Independent of non-RT behavior

Any violation breaks the core design guarantee.

---

### 2.2 Directional Dependencies

Higher-level layers may depend only on lower-level layers.
Lower-level layers must never depend on higher-level layers.

## Layer Order (from lowest to highest)
apps
  ↓
nonrt
  ↓
rt/*
  ↓
exec
  ↓
hal
  ↓
sys
  ↓
core


## Dependency Rule

A layer may depend only on layers listed below it.

Example:

- `rt/*` may depend on `exec`, `hal`, `sys`, `core`
- `exec` may depend on `hal`, `sys`, `core`
- `core` must not depend on any other layer


This ensures:

- Deterministic transport
- Replaceable logging
- Safe degradation
- Portable HAL implementation

---

### 2.3 Replaceability

Each layer must be swappable:

- HAL implementations can be replaced per platform
- Logging backends can be replaced without touching RT
- Execution policies can change without modifying tasks
- Applications can be replaced without modifying framework

If replacing a layer requires editing lower layers,
the layering model is violated.

---

## 3. Layer Responsibilities

### 3.1 core

Minimal compile-time infrastructure:

- Strong types
- Result codes
- Concepts
- Static contracts

Contains no platform assumptions.
Contains no RT assumptions.

---

### 3.2 sys (Portability Layer)

Provides:

- Architecture detection
- Compiler detection
- Cache line configuration
- Memory fence abstraction
- RT configuration flags

It centralizes all platform variability.

---

### 3.3 hal (Interfaces Only)

Defines abstract hardware interfaces:

- Tick source
- GPIO
- ADC
- Watchdog

It does NOT contain implementation.

Platform-specific code lives in:

```

src/hal/<platform>/

```

---

### 3.4 exec (Execution Model)

Defines:

- Task abstraction
- Execution policies (RT / non-RT)

It controls *how* code executes,
not *what* it does.

---

### 3.5 rt/transport

Provides deterministic, bounded data exchange:

- SPSC ring
- Double buffer

These primitives form the only synchronization boundary
between RT and non-RT domains.

Memory ordering guarantees are centralized here.

---

### 3.6 rt/*

Reusable real-time components:

- Controllers
- FSM
- Sensor validation
- Logging publisher

They must:

- Remain bounded
- Avoid allocation
- Avoid blocking
- Avoid syscalls

They may depend on transport primitives,
but never on non-RT code.

---

### 3.7 nonrt

Non-deterministic infrastructure:

- Ring drain
- Dispatcher
- Backends
- Analytics

This layer may block, allocate, or use OS services.

It must not introduce backpressure into the RT layer.

---

### 3.8 apps

Reference or production systems.

Applications assemble:

- HAL implementations
- Execution policies
- RT components
- Non-RT infrastructure

Applications must not modify framework internals.

---

## 4. RT Path Definition

The RT path consists of:

- exec (RT policy)
- rt/transport
- rt/*
- HAL interfaces (only their usage, not implementation)

The RT path must never call:

- nonrt/*
- OS APIs
- Blocking primitives
- Dynamic allocation

All RT-to-nonRT communication must go through transport primitives.

---

## 5. Logging Model

Logging is treated as a subsystem, not a core dependency.

RT publishes:

- State snapshots
- Events
- Diagnostic logs

Non-RT consumes and persists them.

The RT side does not depend on any persistence guarantee.

Loss is allowed if architecturally specified.

---

## 6. Safety Model

Safety mechanisms (e.g., safety_fsm) are:

- Deterministic
- Side-effect bounded
- Independent from logging

Safety escalation must not depend on non-RT response.

---

## 7. Extensibility Rules

To add a new:

### Transport primitive:
Add under `rt/transport`.
Do not depend on exec or nonrt.

### Backend:
Add under `nonrt/backend`.
Must not modify RT code.

### Platform:
Implement HAL interfaces under `src/hal/<platform>`.

### Application:
Add under `apps/`.

If extension requires modifying lower layers,
the architecture must be reconsidered.

---

## 8. Review Checklist

The architecture is considered valid if:

- RT code path contains no blocking or allocation
- Non-RT stalling does not affect RT execution
- Platform code is confined to src/hal
- No upward dependencies exist
- Logging can be disabled without breaking RT

---

## 9. Summary

The layering model enforces:

- Determinism
- Isolation
- Replaceability
- Portability
- Safety

It is not optional.
It is the foundation of the framework.
```

