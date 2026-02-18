# Dependency Graph

This document defines the **strict dependency direction** of the RT framework.

The rules below are architectural invariants.
Violating them breaks the design contract.

---

## 1. Layering Overview

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


Arrows point in the direction of allowed dependencies.

Lower layers must NOT depend on higher layers.

---

## 2. Layer Responsibilities

### core
Minimal, pure C++ utilities:
- types
- result codes
- concepts
- compile-time contracts

No platform assumptions.
No RT assumptions.

---

### sys
Portability layer:
- compiler detection
- architecture detection
- cache line definitions
- fences
- RT configuration flags

May depend on `core`.
Must not depend on `hal`, `rt`, or `nonrt`.

---

### hal (interfaces only)
Hardware abstraction interfaces.

Defines:
- tick source
- GPIO
- ADC
- watchdog

Must not contain platform-specific implementation.

Depends only on:
- `core`
- `sys`

---

### exec
Execution model:
- task abstraction
- RT execution policy
- non-RT execution policy

Depends on:
- `core`
- `sys`
- `hal` (interfaces only)

Must not depend on:
- `rt/logging`
- `nonrt`

---

### rt/transport
RT-safe data exchange primitives:
- SPSC ring
- double buffer

Depends on:
- `core`
- `sys`

Must not depend on:
- `exec`
- `hal`
- `nonrt`

---

### rt/*
Reusable RT components:
- controllers
- FSM
- sensor validation
- logging publisher

Depends on:
- `core`
- `sys`
- `rt/transport`
- optionally `exec`

Must not depend on:
- `nonrt`

---

### nonrt
Non-real-time infrastructure:
- ring drain
- dispatcher
- backends
- analytics

Depends on:
- `core`
- `sys`
- `rt/transport`
- `rt/logging`

Must not depend on:
- `apps`

---

### apps
Reference or production applications.

Depends on:
- everything above

Must not be depended on by any framework layer.

---

## 3. Forbidden Dependencies

The following are explicitly forbidden:

- `rt/*` → `nonrt/*`
- `rt/transport` → `exec`
- `sys` → `hal impl`
- `core` → anything else
- `apps` → modifying framework internals

---

## 4. Physical Separation Rules

- Platform implementations live in `src/hal/<platform>`
- RT path must remain header-only where possible
- Non-RT backends may use `.cpp` implementations
- `include/hal` contains only interfaces

---

## 5. Rationale

This layering guarantees:

- RT determinism is isolated from non-RT behavior
- Portability is centralized
- Logging is optional and replaceable
- Showcase apps do not contaminate framework design
- Each layer is testable in isolation

---

## 6. Industrial Review Criteria

A reviewer should be able to verify:

- RT code path contains no syscalls or allocation
- Memory ordering is confined to transport primitives
- HAL implementation can be swapped without affecting logic
- Non-RT pipeline can stall without blocking RT

If these properties hold, the framework preserves its design contract.