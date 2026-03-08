Safety Thinking
RT System Architecture Checkpoint (rev. 2)
Status

This document captures architectural invariants and responsibility boundaries.
It is not a final specification, but a reference model for further formalization.

1. Two non-mixable safety layers
1.1 Catastrophic safety (hardware layer)

Implemented outside the RT library:

watchdog reset

active-low TRIP with hardware latch

external safety MCU

power/output shutdown

Properties:

monotonicity (no return without reset)

minimal latency

zero trust in software

Invariant:
The RT library does not implement catastrophic safety and does not depend on it.

1.2 Managed degradation safety (RT layer)

Purpose:

controlled behavior degradation before hardware TRIP

preserve system controllability

prevent catastrophe

Library role:

provides a degradation mechanism,
but does not guarantee sufficiency.

2. Ban on lossy data in the safety loop

Forbidden for safety decisions:

event rings

log rings

any queue with possible loss

Reason:

no delivery guarantee

no observability guarantee

behavior is not provable

Invariant:
Safety decisions are made only from states, never from events.

3. The only allowed carrier of safety state
State-snapshot model

Implementation:

DoubleBuffer<T>

Properties:

lossless last-writer-wins

wait-free read

bounded write

deterministic observability

Consequence:

All safety decisions are based only on snapshot states.

4. RT-level safety plane architecture
4.1 Safety tasks (application layer)

run in RT domain

publish:

DoubleBuffer<SafetyState_i>

Features:

distributed risk-assessment sources

logic fully belongs to the application

4.2 Global Safety Administrator (RT, application)

Functions:

reads all SafetyState_i

aggregates according to application logic

publishes:

TaskSafetyState[task]
SystemSafetyState (optional)

Principle:

Observation is distributed, decision is centralized.

4.3 Scheduler (library layer)

Role:

does not make decisions

mechanically applies safety states:

skip

throttle

stop

panic loop

It is an executor, not an analyst.

5. Critical components and observability
5.1 Critical tasks

Tasks with flag:

critical = true

must:

update state regularly

remain observable within the configured time window

5.2 Loss of observability

If:

a critical task

or the Safety Administrator

stops updating state
(with hysteresis/timeout considered),

this is treated as:

catastrophic failure.

Consequence:

stop feeding watchdog

hardware TRIP / reset

"silent survival" mode is forbidden.

6. Library responsibility boundary
6.1 Library provides

deterministic RT runtime

state-based publication mechanism

execution metadata and scheduler hooks

state observability infrastructure

degradation application mechanisms

6.2 Library does not provide

safety logic

degradation criteria

protection sufficiency guarantees

catastrophic hardware safety

recovery policy

Formula:

Library = mechanism
Application = policy + safety case

7. Relation between degradation and hardware catastrophe

Hierarchy:

Hardware catastrophic safety
        ↑
RT managed degradation
        ↑
Normal execution

Meaning:

degradation exists only until TRIP

after TRIP:

either system is stopped by hardware

or RT runtime loses meaning

8. Safety chain liveness

Not owned by the library.

Provided by the application:

internal watchdog mechanisms

external independent watchdog MCU

hardware shutdown circuits

Determined by system criticality class,
not by runtime architecture.

9. Final invariants of this checkpoint

Catastrophic safety is outside the library.

Lossy data is forbidden in safety.

The only source is state snapshots (DoubleBuffer).

Safety logic belongs to the application.

Scheduler is executor only.

Library provides mechanism, not guarantee.

Degradation makes sense only until TRIP.

Safety Administrator is a critical component.

Loss of observability of a critical component = TRIP.

10. Scope of next formalization

Still not defined:

transition lattice of TaskSafetyState

formal ABI of TaskState

WCET bounds for Safety Administrator

rules for recovery after degradation

interaction with external safety MCU

This is the next stage:
transition from architectural thinking to a provable specification.
