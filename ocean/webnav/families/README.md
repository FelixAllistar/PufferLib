# WebNav family infrastructure: 5.0 conversion

The ABI v2 loader, stock Bend transport, serialized incremental builder and
resource guards are preserved from `5c`. Five existing standalone families
(panel/menu/forms/numeric/click) and the shared public scripted runner are ported;
calendar, controls and email are now integrated and checked as well.
The eleven remaining legacy task ports now build and pass family checks in
click, forms, tree and autocomplete. The combined legacy training/checkpoint
workflow remains separate. The click and forms families have
[click](../../webnav_family/README.md) and [forms](../../webnav_forms/README.md)
trainable adapters with original-browser evaluations. Panels, menus, and
numeric remain conformance and scripted-solvability benchmarks without a
learned policy path.
The three-task pilot is not a replacement for the task families.

The [audit and parallel queue](PARALLEL_PLAN.md), updated 2026-09-29, records
125 checked current-family tasks across 23 families, with no pending names or legacy ports.
The final editing/visual/drag packages passed 57 laws, 468 original-page
episodes, and 24,000 full-credit generated public scripted episodes.
The legacy port wave passed 1,060 original-page episodes and 29,000 public
scripted generated episodes across click, forms, tree and autocomplete; old
click/forms native training adapters also pass their compatibility checks.
Social, composite forms and scroll additionally pass 240 original-page episodes
and 12,000 public scripted episodes. Catalog, travel and typed inputs add
180 original-page episodes and 9,000 public scripted successes, bringing this
turn's total to 21 new names plus eleven legacy ports. The earlier
calendar/controls/email wave passed 47 laws, native tests, 420 matched
original-page episodes and 21,000 public scripted episodes. Family READMEs and
RESULTS files describe the supported actions and remaining parity gaps.
The user confirmed the sweep stopped; root may validate serially under the
existing guard. Workers remain source-only and use the `bend-cpu-families`
skill referenced by `AGENTS.md`.

`make -f ocean/webnav/families/Makefile registry` validates the 125 registered
task entries. `node ocean/webnav/families/registry.cjs --verify-source` also
checks all 130 HTML fingerprints against the reference installed by
`ocean/webnav/setup_miniwob.sh`. The latter check passed on 2026-09-29.
Registry `legacy-bounded` is retained as a historical status but no current
task uses it after the 2026-09-29 port wave. Every registered name now has checked behavior under its family’s documented
action and generation presets. This is task-name coverage, not full browser
fidelity or 125 trained policies.

Read `AGENTS.md` and `BUILD_SAFETY.md` before family work. Every actual family
build must go through `node ocean/webnav/families/build.cjs FAMILY --test`.
The wrapper requires Linux cgroup v2 and a working user systemd session,
limits the whole process tree to at most 6 GiB without swap, and holds an OS
lock. There is no unrestricted fallback.

Dependencies: Node.js, Bash, flock, systemd-run, Clang 19, stock Bend 2.0.6 and
Bun. The default compiler wrapper reads Bend from `$HOME/.bend` (`BEND_HOME`)
and Bun from `$HOME/.bun/bin/bun` (`BUN_BIN`). Generated files and libraries
remain under `build/webnav/families/`, not in Git.

The cached Bend guide required by `AGENTS.md` is preserved in both local
checkouts at `build/webnav/task-expansion/bend-guide-families.txt`. A new clone
can obtain the guide with `bend guide` using the pinned stock 2.0.6 compiler;
dependency/source provenance remains part of the full WebNav asset audit.

Qualification on 2026-09-24:

- `make -f ocean/webnav/families/Makefile test-loader` passes ABI validation,
  public text bounds, clocks, invalid rows, library isolation and reopen tests.
- The resource guard rejects an unrestricted invocation and accepts a real
  64 MiB, no-swap systemd scope.
- Shell and JavaScript syntax checks pass.

These are infrastructure checks. They do not establish any family's browser
parity, behavior coverage or learned-policy success.
