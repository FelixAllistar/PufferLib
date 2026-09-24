# Family build resource limits

Use `node ocean/webnav/families/build.cjs FAMILY --test` or the family
Makefile. The entry point automatically enters a user systemd scope with:

- Up to 6 GiB memory for the whole build process tree; no swap. At launch the
  cap is reduced if needed to leave 768 MiB of currently available host memory.
- One family build at a time through an OS file lock.
- One CPU worth of execution, 128 tasks, and a five-minute timeout.
- Verification of the actual cgroup memory limits before any compiler starts.

Missing systemd or cgroup support is an error, with no unrestricted fallback.
`bend_cpu.sh` also requires an existing limited scope. Do not run stock Bend
directly for these families to bypass this protection. Older MiniWoB scripts,
standalone browser tests, and unrelated training commands are not covered by
this family build wrapper.

## Incident, 2026-09-23

Before a WSL restart, click-family CPU code generation was observed using over
6 GiB RSS in a VM with about 7.7 GiB RAM and nearly full 2 GiB swap. Bun's
`--smol` option did not contain the growth. The last saved build log stops at
code generation. Following restart, journals showed an unclean shutdown; no
saved global OOM record was found in the searched incident interval. Memory
exhaustion is the leading hypothesis, not a proven cause of the VM failure.

No compiler jobs survived the restart. The user authorized resuming with a 6 GiB cap after the initial 2 GiB
containment check. Investigate source/compiler growth within this cap. Reduce compilation units or
source complexity within the limit; do not raise it automatically.

The click build subsequently completed within the cap after replacing bounded
U32 literal dispatch with Nat dispatch in the synonym table, generator and
wire decoding. Its native and original-browser checks pass. This identifies
a practical source-level workaround; it does not prove a compiler root cause.

Validation: an unrestricted resource check was rejected; a 64 MiB limited
check passed. A separate allocation probe in that 64 MiB scope exited 137;
the kernel recorded `CONSTRAINT_MEMCG` and killed only the probe. WSL remained
responsive and swap usage remained zero. This intentional test OOM occurred
at 00:26:44 local time and must not be confused with the earlier incident.
