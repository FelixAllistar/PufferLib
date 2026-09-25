# WebNav family infrastructure: 5.0 conversion

The ABI v2 loader, stock Bend transport, serialized incremental builder and
resource guards are preserved from `5c`. All five existing standalone families
(panel/menu/forms/numeric/click) and the shared public scripted runner are ported.
The historical coverage registry is restored; legacy training/runtime workflows
still need conversion.
The three-task pilot is not a replacement for the task families.

`make -f ocean/webnav/families/Makefile registry` validates the 125 registered
task entries. `node ocean/webnav/families/registry.cjs --verify-source` also
checks all 130 HTML fingerprints against the reference installed by
`ocean/webnav/setup_miniwob.sh`. The latter check passed on 2026-09-24.
Registry `legacy-bounded` entries describe the preserved 5c work, not completed
5.0 runtime ports. Planned families remain planned; this migration does not
claim to implement tasks that were never implemented in the original fork.

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
