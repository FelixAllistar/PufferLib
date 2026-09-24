# Family implementation rules

- Task behavior and instance generation belong in stock CPU Bend. C is for
  transport, public observation projection, and independent test oracles.
- Read the Bend guide and a completed family before adding one. The cached
  stock 2.0.6 guide is `build/webnav/task-expansion/bend-guide-families.txt`
  relative to the repository root. Audit the pinned original HTML yourself.
- Use `node ocean/webnav/families/build.cjs FAMILY --test` for builds. It
  serializes the complete build tree and enforces the user-approved 6 GiB
  memory cap with no swap. Do not bypass it with direct compilation or launch
  concurrent compiler jobs. See BUILD_SAFETY.md for the WSL incident.
- Prefer typed variants or Nat dispatch for small bounded identifiers. Large
  U32 literal matches caused excessive stock compiler memory use in click.
- Count a task only after laws, independent native tests, original-browser
  comparisons and public-observation solvability checks pass. Keep bounded
  behavior coverage, full parity and learned PPO success distinct.
- Shared registry/inventory edits belong to the integrating agent. Separate
  family workers should stay in their assigned directories.
