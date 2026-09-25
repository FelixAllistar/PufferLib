# Legacy MiniWoB simulator conversion

Status: source preservation, **not a qualified fresh native build**. The
combined compiler has exceeded the safe local memory allowance. Do not infer
that the command below succeeds from the presence of these files. Existing
legacy-generated-library tests are separate evidence, not a substitute for
fresh code generation. The entry/wire sources and original contract tests are
byte-identical to the preserved main checkout; no reduced simulator replaces
the twelve presets. The launcher shares the family build lock and still
requires a verified limited scope.

This is the existing shared stock CPU Bend 2.0.6 simulator, separate from both
the three-task pilot and the five standalone families. Its twelve bounded
presets are click-button, click-link, focus-text, click-checkboxes, click-option,
enter-text, login-user, read-table, click-button-sequence, choose-list,
navigate-tree, and use-autocomplete-nodelay.

The simulator transports 32 independent rows of 256 uint32 words. Task tags
0 through 10 select the transition model; click-button and click-link share a
tag. Tag 11 requests a generated reset for one of the twelve training presets.
These rows contain private goals and must never be passed directly to a policy.
The public observation/training adapter is a separate pending conversion.

Models, generators, validation and native tests come from the legacy branch.
The private wire, task logic and generators are unchanged. Generator
distributions are bounded subsets, not replicas
of the original browser RNG, full geometry, or unrestricted keyboard behavior.
ASCII text fields are limited to 64 characters, or 32 per field for login.
Tree states contain at most eight nodes; sequence training layouts avoid
overlapping buttons. Held-out reset seeds do not imply unseen vocabulary.

Build and run the native contract suites from the repository root, with no
other Bend compilation running:

```sh
systemd-run --user --scope -p MemoryMax=4G -p MemorySwapMax=0 \
    -p CPUQuota=100% -p TasksMax=128 \
    timeout --signal=KILL 300 bash ocean/webnav/miniwob/native_test.sh
```

The cap must leave memory for the host. Requires Clang 19 and stock Bend 2.0.6;
The default launcher uses Bun's low-memory mode and verifies the cgroup limit;
`BEND_BIN` can select another stock compiler launcher. An ordinary Bun launch
hit the 4 GiB cap during conversion; do not remove the cap to work around it.
Outputs and source hashes are under ignored
`build/webnav/`. Browser oracles and the complete training workflow still need
porting; native tests alone do not certify original-browser parity or learning.
