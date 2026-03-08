# Layering Model

This file defines the repository layering used for development and review.
It is aligned with current code layout and CMake targets.

## 1. Layers (Low -> High)

1. `primitives`
2. `stam-rt-lib`
3. `modules`
4. `apps`

Dependency direction is only upward (high depends on low).

## 2. Layer Responsibilities

### 2.1 primitives

Location:

- `primitives/include/stam/primitives/*`
- `primitives/include/stam/sys/*`

Responsibilities:

- RT data-exchange primitives (`SPSC`, snapshots, mailbox, double-buffer variants)
- portability/system headers (`sys_*`)

Constraints:

- deterministic API contracts for RT path
- no dependency on `stam-rt-lib`, `modules`, `apps`

### 2.2 stam-rt-lib

Location:

- `stam-rt-lib/include/model/*`
- `stam-rt-lib/include/exec/*`
- `stam-rt-lib/rtr/*`

Responsibilities:

- model contracts (`tags`, `PortName`, `BindResult`)
- bootstrap channel binding (`ChannelWrapper`, `ChannelRef`)
- task execution adapters (`TaskWrapper`, `TaskWrapperRef`)
- system readiness validation (`TaskRegistry::seal`)
- runtime stub (`stam_rtr`)

Constraints:

- may depend on `primitives`
- must not depend on `modules` or `apps`

### 2.3 modules

Location:

- `modules/logging/*`
- `modules/demo/*`

Responsibilities:

- reusable feature modules built on top of execution/model layer

Constraints:

- may depend on `stam_exec` (and transitively on lower layers)
- must not depend on `apps`

### 2.4 apps

Location:

- `apps/minimal`
- `apps/demo/trivial_tasks`
- `apps/brewery`

Responsibilities:

- integration/examples/product composition

Constraints:

- may depend on all lower layers
- must never be imported by framework layers

## 3. Bootstrap vs Runtime Boundary

System integration must follow the same phase split across layers:

1. bootstrap: construct, bind, register, seal
2. runtime: scheduler/task execution only

No bind/rebind operations are allowed after successful runtime start.

## 4. Review Checklist

A change is layer-safe if:

- no lower layer include/link references upper layers
- `stam-rt-lib` public headers remain independent from `modules/apps`
- runtime path remains free from bootstrap reconfiguration logic
- app-specific decisions stay inside `apps/*`
