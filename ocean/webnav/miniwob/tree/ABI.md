# Navigate-tree CPU row ABI

The tree model is a matched-instance simulator.  The host imports the tree
shape and the private `target` predicate produced by the original HTML task;
the policy-facing DOM observation remains the caller's responsibility.

Each lane is one 256-word `Array<U32>`.  `Wire.row(base, a)` consumes and
rewrites one lane in place.  `base` is the lane offset (`lane * 256`).
The root dispatcher must call `webnav_tree_valid(r)` from `validation.h` before
entering Bend; the pure model assumes this shape contract for parent walks.

Header words:

| word | meaning |
| ---: | --- |
| 0 | task tag, always `9` |
| 1 | node count `N`, `0..8` |
| 2 | outcome: `0` running, `1` finished, `2` timed out |
| 3 | last accepted node, one-based; `0` before a click |
| 4 | finished success bit; `0` for running/timeout or wrong leaf |
| 5 | terminal denominator (`1` for this binary task) |
| 6 | time-scaled bit; `1` only for successful completion |
| 7 | command: `0` wait, `1` click, `2` timeout, `3` reset |
| 8 | node index for command `1`, zero-based |
| 9 | logical elapsed milliseconds supplied by the host |
| 10 | frozen terminal elapsed milliseconds |
| 11 | frozen raw reward (`F32.bits`) |
| 12 | frozen time-adjusted reward (`F32.bits`) |

Nodes start at word 16, with a six-word stride:

| offset | meaning |
| ---: | --- |
| +0 | kind: `0` file/leaf, `1` folder |
| +1 | private target bit: label equals the requested name |
| +2 | folder expansion bit |
| +3 | parent one-based index; `0` means a root node |
| +4 | pre-order subtree end, exclusive; leaf is `i+1` |
| +5 | derived visible bit written by Bend |

The original generator caps the tree at eight nodes and depth two.  A node is
visible when every folder on its parent chain is expanded; all folders start
collapsed.  A label or hitarea click toggles only the clicked folder's direct
child `<ul>` before the custom `li` handler runs.  Clicking a visible target
node finishes with `+1`.  Clicking a visible wrong file finishes with `-1` and
stops propagation.  A wrong folder returns `undefined`, so the same event
bubbles through ancestor `li` handlers; a target folder ancestor can therefore
finish the episode, while wrong folder ancestors keep bubbling.  Hidden and
out-of-range node actions are identity actions.  Timeout at elapsed `>= 10000`
wins over a same-step click.

The `target` bit is transport-private and must be populated from the original
label predicate (`node.text === expectedName`), so repeated labels preserve the
upstream "any matching node" behavior.  Reset command `3` starts a new logical
elapsed-time epoch and clears expansion, terminal and reward fields.  This row
is not the policy DOM-v2 observation ABI.
