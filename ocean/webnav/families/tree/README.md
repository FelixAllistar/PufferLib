# Navigate tree family

The preserved CPU Bend tree model and generator now run behind the ABI-v2
family loader. They no longer depend on compiling the old combined MiniWoB
dispatcher. Task ID 0 is `navigate-tree`; rows are 512 U32 words, eight lanes,
with the preserved 256-word body at offset 32. Behavior is reused from
`miniwob/tree/Tree.bend`, transport from its `Wire.bend`, and generation from
`miniwob/training/TreeGenerate.bend`.

The source audit follows the pinned MiniWoB++ revision
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614`, `navigate-tree.html` and its treeview
plugin. A matching visible name succeeds, including repeated names. A wrong
file ends with failure. A wrong folder toggles its branch and normally keeps
running; its click can bubble to a matching ancestor. Only the clicked folder
toggles. Closed ancestors hide descendants. The ten-second deadline preempts
a simultaneous click; successful rewards scale with elapsed time.

Public observations contain visible node names, parent refs, roles and branch
expansion flags. Hidden labels and private target bits stay out of the view.
The controller opens folders until it finds the requested public name. C only
adapts transport, projects these fields and supplies independent oracles.

The guarded build checks 22 tree laws and three generator laws. The preserved
independent native suite now runs against the freshly compiled family, followed
by 256 generated public solves and target noninterference checks. A further
1,000/1,000 generated public episodes pass. The original-browser oracle passes
100 seeded episodes and 171 action comparisons, including 16 explicit branch
collapse schedules, wrong files and timeouts. These are scripted checks, not
trained-policy scores. See [RESULTS.json](RESULTS.json).

Bounds remain explicit: at most eight nodes and depth two, finite generated
labels and six topology presets, normalized public geometry, CDP span clicks,
settled treeview state and a controlled logical clock. Hit-area-only clicks,
complete layout and source generation distributions are not covered. The old
legacy library and checkpoints are retained.

```sh
make -f ocean/webnav/families/Makefile browser FAMILY=tree EPISODES=100
make -f ocean/webnav/families/Makefile public-check FAMILY=tree EPISODES=1000
```
