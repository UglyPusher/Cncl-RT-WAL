# Concepts Disclaimer

`docs/concepts/` contains only:

- reasoning history,
- draft discussions,
- intermediate snapshots of decisions.

This directory is **not** normative documentation and **not** source-of-truth for implementation contracts.

## Mandatory Rule

Do not use files from `docs/concepts/*` as a final specification for coding/review decisions.

## Source-of-Truth Locations

Use these directories instead:

- repository architecture: `docs/architecture/*`
- primitive contracts: `primitives/docs/*`
- runtime library contracts: `stam-rt-lib/docs/*`
- portability/safety guidance: `docs/portability/*`, `docs/safety/*`

## Practical Workflow

1. Concepts may be used for brainstorming only.
2. Final decisions must be migrated into source-of-truth docs.
3. If a statement exists only in `docs/concepts/*`, treat it as non-final.
