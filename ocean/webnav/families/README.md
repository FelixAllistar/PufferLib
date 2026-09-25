# WebNav family infrastructure: 5.0 conversion

The ABI v2 loader, stock Bend transport, serialized incremental builder and
resource guards are preserved from `5c`. Task directories, registry and public
controllers are still pending their individual ports; the three-task pilot
is not a replacement for them.

Read `AGENTS.md` and `BUILD_SAFETY.md` before family work. Every actual family
build must go through `node ocean/webnav/families/build.cjs FAMILY --test`.
The wrapper requires Linux cgroup v2 and a working user systemd session,
limits the whole process tree to at most 6 GiB without swap, and holds an OS
lock. There is no unrestricted fallback.

Dependencies: Node.js, Bash, flock, systemd-run, Clang 19, stock Bend 2.0.6 and
Bun. The default compiler wrapper reads Bend from `$HOME/.bend` (`BEND_HOME`)
and Bun from `$HOME/.bun/bin/bun` (`BUN_BIN`). Generated files and libraries
remain under `build/webnav/families/`, not in Git.

The cached Bend guide required by `AGENTS.md` is currently preserved at
`/home/felix/puffertank/pufferlib/build/webnav/task-expansion/bend-guide-families.txt`.
It must be carried into the canonical checkout's corresponding build path
before the old checkout is retired; dependency/source provenance remains part
of the full WebNav asset audit.

Qualification on 2026-09-24:

- `make -f ocean/webnav/families/Makefile test-loader` passes ABI validation,
  public text bounds, clocks, invalid rows, library isolation and reopen tests.
- The resource guard rejects an unrestricted invocation and accepts a real
  64 MiB, no-swap systemd scope.
- Shell and JavaScript syntax checks pass.

These are infrastructure checks. They do not establish any family's browser
parity, behavior coverage or learned-policy success.
