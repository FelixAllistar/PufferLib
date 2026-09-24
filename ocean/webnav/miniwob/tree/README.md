# CPU Bend `navigate-tree`

This directory is a matched-instance CPU model of the pinned MiniWoB++
`navigate-tree` task at revision `33c3b4ddef8c6eb67c57a29663d844b1eda7e614`.
The source audit used
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/navigate-tree.html`,
the bundled `jquery.treeview.min.js`, its CSS and image assets, and the task's
`core.js`/`ui_utils.js`.

The original generator appends a pre-order tree with at most eight nodes and
depth two.  `jquery.treeview({collapsed: true})` hides every child `<ul>` at
episode start.  A folder has a child `<ul>` even when it is empty, so clicking
its label or hitarea toggles that folder's expansion.  The custom `li` handler
then checks the clicked label.  A matching label ends the episode with `+1`; a
wrong file ends it with `-1`; a wrong folder returns `undefined`, allowing the
event to bubble through ancestor `li` handlers.  Consequently a target folder
ancestor can succeed after a wrong descendant-folder click, while a wrong
descendant file stops propagation and fails.  Repeated labels are all valid
targets because the original predicate is `fileText === expectedName`.

The Bend model represents the imported pre-order topology with one-based
parent links and exact subtree ends.  It computes visibility from the expanded
ancestor chain, toggles only the clicked folder, and evaluates the bubbling
chain before writing the terminal state.  Timeout at 10,000 logical
milliseconds preempts a same-step click.  Successful completion is time scaled;
wrong leaves and timeout remain `-1`.

`ABI.md` defines the private tag-9 row transport.  The target bit is host
transport data derived from the original label equality and is not a policy
observation.  `validation.h` rejects malformed topology, booleans, depth and
time state before Bend runs; reset command `3` starts a new elapsed-time epoch.

Run the standalone proof, generated C bridge, validator tests and 32-lane CPU
checks with:

```sh
bash ocean/webnav/miniwob/tree/build.sh
```

The standalone browser differential harness lives with the shared webnav
integration.  It exercises seeded original HTML trees plus directed fixtures
for ancestor bubbling, wrong-leaf propagation, duplicate labels and collapsed
visibility.  This model does not claim arbitrary coordinate hit testing,
renderer/occlusion parity, or unrestricted DOM accessibility-tree parity.
