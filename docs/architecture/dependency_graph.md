# Dependency Graph

This document defines the current dependency graph of this repository.
It reflects actual CMake wiring (not a target architecture proposal).

## 1. Build Units

Top-level order (`/src/CMakeLists.txt`):

1. `primitives`
2. `stam-rt-lib`
3. `modules` (optional)
4. `apps` (optional)

## 2. Target-Level Graph

```text
stam_primitives (INTERFACE)
    ↑
stam_model (INTERFACE)
    ↑
stam_exec (INTERFACE)  ---->  stam_primitives
    ↑
stam_rtr (STATIC, optional)

module_logging (STATIC)  ---->  stam_exec
demo_trivial_tasks (STATIC) --> stam_exec

app_minimal      ----> stam_exec + stam_rtr + module_logging
app_trv_task     ----> stam_exec + stam_rtr + demo_trivial_tasks
app_brewery      ----> stam_exec + stam_rtr + module_logging
```

## 3. Directory-Level Dependency Direction

```text
apps
  ↓
modules
  ↓
stam-rt-lib
  ↓
primitives
```

Meaning:

- upper directory may depend on lower one
- lower directory must not depend on upper one

## 4. Enforced / Forbidden Dependencies

### Enforced by CMake now

- `stam_exec` links `stam_model` and `stam_primitives`
- all shipped modules link `stam_exec`
- all shipped apps link `stam_exec` and `stam_rtr`

### Forbidden by architecture contract

- `primitives` -> `stam-rt-lib`
- `primitives` -> `modules` / `apps`
- `stam-rt-lib` -> `modules` / `apps`
- `modules` -> `apps`

## 5. Notes

- `stam_primitives` is header-only and includes both transport primitives and `stam/sys/*` portability headers.
- `stam_model` and `stam_exec` are interface libraries in current implementation.
- `stam_rtr` is currently a minimal static runtime stub over `stam_exec`.
