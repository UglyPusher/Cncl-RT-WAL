# ChannelRef - Type-Erased View Contract

## 0. Scope

`stam::model::ChannelRef` is a non-owning, type-erased view used by `TaskRegistry::seal(...)` to validate channel readiness.

Definition:

- `const void* obj`
- `bool (*is_fully_bound_fn)(const void*) noexcept`
- `const char* name`

## 1. Construction Contract

`make_channel_ref(C& ch, const char* name)` is the canonical builder.

Requirements:

- `C` satisfies `ChannelLike` (`c.is_fully_bound() const noexcept -> bool`)
- input must be lvalue reference (`C&`)
- rvalue overload is deleted by design

Rationale:

- prevents dangling `obj` from temporary channels

## 2. Lifetime Contract

`ChannelRef` does not own target object.

Caller must guarantee:

- `obj` stays alive until `seal(...)` completes
- referenced channel is not moved/destroyed while referenced by `ChannelRef`

Typical safe pattern:

- keep channel objects in bootstrap scope
- build `std::array<ChannelRef, N>` from lvalue channels
- call `registry.seal(span)` in same scope

## 3. Name Field Semantics

`name` is optional diagnostic label.

- may be `nullptr`
- if provided, must outlive seal call
- returned via `SealResult::failed_name` for channel-side failures

## 4. Failure Surface

At seal time, channel is treated as unbound if:

- `is_fully_bound_fn == nullptr`, or
- `is_fully_bound_fn(obj) == false`

This maps to `SealResult::Code::channel_unbound`.

## 5. Threading and Phase

`ChannelRef` is bootstrap-only wiring data.

Concurrent mutation of referenced channels during seal is outside contract.
