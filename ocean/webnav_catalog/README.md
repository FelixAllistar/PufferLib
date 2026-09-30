# Catalog learner adapter

This native PufferLib adapter connects `phone-book`, `order-food`, and
`search-engine` to PPO. It loads the existing `catalog` stock CPU Bend DSO.
Bend owns generation, transitions, clocks, and rewards. The adapter projects
`WFView` into 6,352 float features and 67 actions: wait, one click per public
node (up to 64), select all, and insert a literal quoted instruction span.
Four lanes share a Bend batch; the default vector has 128 agents. Every action
advances 250 ms.

The policy sees public instruction and node text, roles, flags, stable refs,
page numbers, quantities, the instruction's numeric result ordinal, and its
own click counts. Food name/type and phone name matches are computed only from
visible text. It never receives Bend goal fields, task IDs, seeds, reward
codes, or a scripted target action. Search result ordinals stay numeric even
when original titles duplicate. Search text insertion copies the quoted span
verbatim; the policy still chooses when to focus, select all, insert, search,
change page, or click a result. Loaded results retain their displayed refs
after editing the input, matching the source page's stale handler behavior.

`perf` is full raw reward only. `score` retains the raw score, including the
phone book's positive partial credit. The native and browser evaluators report
both full wins and partial positive outcomes. Browser evaluation reconstructs
the same public projection from the pinned original pages and uses actual CDP
click and text events; it does not import a private generated instance.

The shared build now recognizes `webnav_catalog`. Run from the repository
root, keeping builds and training serialized under the documented resource guard:

```sh
node ocean/webnav/families/build.cjs catalog --test
make -f ocean/webnav_catalog/Makefile test browser-build build/webnav_catalog/native_eval
./build.sh webnav_catalog build/webnav_catalog_train --float
build/webnav_catalog_train train --train.total_timesteps=3000000
build/webnav_catalog/native_eval random 100
build/webnav_catalog/native_eval CHECKPOINT 100
build/webnav_catalog/browser_eval CHECKPOINT 20
```

The trainer uses H64/L1 by default and `env.task_mask=7` selects all three
tasks; masks 1, 2, and 4 select one task. Both evaluators accept a final task
name. `WEBNAV_SEED_OFFSET` changes original-browser seeds. The browser
evaluator defaults to the pinned headless Chromium shell under `build/webnav`.
Short training, native evaluation and original-page evaluation have now run; see [RESULTS.md](RESULTS.md). Source parity
for the underlying bounded Bend catalog family is recorded separately in
`ocean/webnav/families/catalog/RESULTS.json`; that record is not a learned PPO
result. Original pages may have duplicate phone-book names, which can make
the correct original contact index ambiguous from the public view. The Bend
generator uses distinct names; this is a source-distribution transfer gap.

Current root validation and measured learned results: [RESULTS.md](RESULTS.md).
