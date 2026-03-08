# Directory Structure (Current)

Snapshot of the actual repository layout.

```text
Cncl-RT-WAL/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── architecture/
│   ├── portability/
│   ├── safety/
│   ├── concepts/
│   └── ...
├── primitives/
│   ├── CMakeLists.txt
│   ├── include/stam/
│   │   ├── primitives/
│   │   └── sys/
│   ├── tests/
│   └── docs/
├── stam-rt-lib/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── model/
│   │   └── exec/
│   ├── rtr/
│   ├── tests/
│   └── docs/
├── modules/
│   ├── CMakeLists.txt
│   ├── logging/
│   └── demo/
└── apps/
    ├── CMakeLists.txt
    ├── minimal/
    ├── demo/trivial_tasks/
    └── brewery/
```

## 1. Practical Ownership Map

- `primitives/`: portability + lock-free/RT primitives
- `stam-rt-lib/`: model, bind/seal, task adapter, runtime stub
- `modules/`: reusable features on top of execution layer
- `apps/`: integration binaries

## 2. Documentation Placement

- repository-wide architecture/process docs: `docs/*`
- primitive-specific contracts: `primitives/docs/*`
- runtime-lib contracts: `stam-rt-lib/docs/*`

## 3. Consistency Rule

When directory layout changes, this file and `dependency_graph.md` / `layering.md` must be updated in the same change set.
