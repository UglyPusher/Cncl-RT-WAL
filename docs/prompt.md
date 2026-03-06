You are reviewing and editing the README of a C++ project called STAM (Separated Task Application Model).

The goal is NOT to redesign the project, but to clarify its architectural purpose.

Important context:

STAM is NOT an RTOS.
STAM is an execution architecture and component model for real-time systems.

The main idea:

RT and non-RT domains are strictly separated.

RT domain:
- bounded deterministic execution
- no allocations
- no syscalls
- no blocking
- no locks
- no IO
- explicit invariants and contracts

non-RT domain:
- persistence
- logging
- analytics
- diagnostics
- configuration
- everything expensive or unpredictable

Applications are built from components connected via explicit communication channels.

Typical channels:
- event stream (SPSC ring)
- snapshot state publication (double buffer / mailbox)
- multi-reader state snapshot (SPMC snapshot)

Each primitive has an explicit contract:
- threading model
- memory ordering
- progress guarantees
- misuse scenarios
- RT constraints

The repository structure:

primitives/
    RT-safe lock-free IPC primitives

stam-rt-lib/
    RT execution model (task wrappers, scheduler-facing abstractions)

modules/
    non-RT infrastructure (logging, persistence)

apps/
    reference applications

The key message of STAM:

STAM provides a contracts-first architecture for deterministic RT systems built from components connected by formally defined channels.

It is intended for embedded/industrial control systems where:

- deterministic RT behavior matters
- RT and non-RT responsibilities must be separated
- communication semantics must be explicit and analyzable

Reference example: brewery control system.

Tasks:
1. Rewrite the README to clearly express the above architectural intent.
2. Remove wording that suggests STAM replaces RTOS kernels.
3. Emphasize that STAM can run:
   - bare metal
   - on top of RTOS
   - on SMP systems
4. Clarify the difference between:
   - RT primitives
   - execution model
   - non-RT modules
5. Keep the README concise and technically precise.
6. Do NOT introduce new architectural concepts not present in the repository.

Output:
A complete revised README.md.