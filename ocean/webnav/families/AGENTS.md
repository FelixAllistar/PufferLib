# Family implementation rules

- Current user coordination rule: on 2026-09-29 the user confirmed "sweep is
  dead", clearing root's build hold. Only the root integrating agent may run
  Bend, compilers, builds, tests or browsers, one family at a time under the
  existing resource guard. Family workers remain restricted to reading and
  editing source, with no builds/checks/proofs/tests/training/benchmarks. They
  never receive build permission from another worker. Read the cached guide
  below instead of running `bend guide`.
- Task behavior and instance generation belong in stock CPU Bend. C is for
  transport, public observation projection, and independent test oracles.
- Read the Bend guide and a completed family before adding one. The cached
  stock 2.0.6 guide is `build/webnav/task-expansion/bend-guide-families.txt`
  relative to the repository root. Audit the pinned original HTML yourself.
- Use the local `bend-cpu-families` skill when available at
  `$CODEX_HOME/skills/bend-cpu-families/SKILL.md` (default
  `~/.codex/skills/bend-cpu-families/SKILL.md`). Its array-read examples address
  recurring handoff errors, including computed tuple destructuring.
- Use `node ocean/webnav/families/build.cjs FAMILY --test` for builds. It
  serializes the complete build tree and enforces the user-approved 6 GiB
  memory cap with no swap. Do not bypass it with direct compilation or launch
  concurrent compiler jobs. See BUILD_SAFETY.md for the WSL incident.
- Prefer typed variants or Nat dispatch for small bounded identifiers. Large
  U32 literal matches caused excessive stock compiler memory use in click.
- Avoid large unary Nat literals such as `8192n`: stock compilation can
  overflow its stack while expanding them. Clear rows in small nested blocks
  (for example, 64 blocks of 128 words in `composite_forms/Generate.bend`).
- Before a source handoff, review Bend's binding rules explicitly: definitions
  precede their callers; computed values reach `match` through helper parameters;
  matches obey binder order and precede lets; reused Data bindings need `+`;
  structurally shrinking recursion arguments come before changing arguments.
  Workers perform this review without invoking the checker.
- Annotate complete U32 operator expressions explicitly, including function
  arguments: `U32.is_eq((index % 2 : U32), 0)`. An enclosing U32 function does
  not make an unannotated arithmetic operator stop defaulting to Nat.
- New family work starts with a small model, one semantic law and a minimal
  wire path, copied from a checked example. Hand that increment to root for
  the first permitted serialized compile before growing the complete family.
  During a build hold, keep the increment explicitly unvalidated; workers
  still do not run the compiler themselves.
- Count a task only after laws, independent native tests, original-browser
  comparisons and public-observation solvability checks pass. Keep bounded
  behavior coverage, full parity and learned PPO success distinct.
- Check reset with dirty rows, generator answer variation, and public-view
  independence from private goal fields. A constant generated answer or only
  successful scripted traces cannot establish useful benchmark coverage.
- Audit the page's actual widget configuration and event closures, not just
  library defaults. Phone-book shows one numeric page plus adjacent arrows;
  search-engine rereads the input when a different page is selected, while
  loaded result handlers retain their earlier query. Compare these intermediate
  states after wrong edits as well as successful traces.
- Read C helper ownership contracts before writing browser fixtures.
  `web_cdp_call` consumes its `params` object; deleting it again is a double
  free. Derive text capacities from pinned source maxima instead of assuming
  short labels also bound distractor text.
- Shared registry/inventory edits belong to the integrating agent. Separate
  family workers should stay in their assigned directories.
