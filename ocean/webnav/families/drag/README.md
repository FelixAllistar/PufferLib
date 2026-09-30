# Drag family

Local IDs: 0 `drag-box`, 1 `drag-circle`, 2 `drag-cube`, 3 `drag-items`,
4 `drag-items-grid`, 5 `drag-shapes`, 6 `drag-shapes-2`,
7 `drag-single-shape`, 8 `drag-sort-numbers`, 9 `resize-textarea`.

Every task has a stock CPU Bend transition, deadline, reward and generator.
The C layer validates actions and projects only visible instructions, widgets,
geometry and labels. Root owns guarded compilation and browser qualification.

`WF_POINTER_DOWN` selects a draggable node, `WF_POINTER_MOVE` requests a
normalized target, and `WF_POINTER_UP` releases it. Box move arguments are
absolute CSS left/top plus 256 (signed range -256..255). SVG move arguments
are transformed bounding-box origins in tenths of a pixel plus 2560. Sortable
move `arg0` is a one-indexed destination; resize move arguments are content
width/height in pixels. The cube move `arg0` requests a settled visible face
1..6. SVG positions snap to the pixel lattice reachable by the original D3
mouse handler, using its shape-specific fractional offset; a request may move
by up to half a pixel. Requests that would snap outside the encoded coordinate
range are rejected. All moves are inert without a held node. The browser
fixture dispatches mouse events through the original D3 and jQuery UI
handlers; sortable moves steer the widget's placeholder to the requested
slot. It compares the resulting page with Bend after each action, without
copying the page's result into the model. The cube preset waits for a settled
numbered face; continuous rotation paths and in-flight animation are outside
this action vocabulary.

The source rule ignores rendered border and padding: success requires each
small-minus-large CSS coordinate to be strictly between 0 and 30. Touching
either edge fails. Generated small left is 0..128, small top 0..74, large
left 0..98, and large top 0..44.

Directional SVG tasks compare the current transformed `getBBox` origin to its
initial origin on one strict axis. `resize-textarea` compares only the queried
content dimension to its initial value. The two item tasks finish on jQuery
sortable stop and require the queried item to have been picked from its
original index; number sorting stays active until Submit and accepts ties.

`drag-shapes` requires all queried types strictly inside its single box and
all other shapes outside. `drag-shapes-2` assigns each of five shapes +0.26
or -0.20 for correct/incorrect side, then clamps the total to [-1,1]. The
second page's random `containerX/Y` is a generation-only exclusion region;
the actual target boxes are fixed at (2,5) and (84,5). The cube begins with
face 6 active even though its viewport's internal `currentSide` starts at 0.

Generated item names are a distinct 12-name subset of the pinned page's name
list. The Bend item generators use positional requests; original pages also
sample top/bottom and relative requests, which the imported-page model and
public controller handle. The Bend `drag-shapes-2` color mode picks a color
present on the first shape, while the page samples uniformly from the distinct
present colors. These generator choices retain valid source instances but
narrow the content distribution. Original-page differential episodes import
each initial page instance once, preserving its full generation distribution.
Pointer actions are bounded by ABI validation; unsupported coordinates are
rejected rather than silently clamped.

Qualification on 2026-09-29 passed 15 laws, independent native checks,
188 original-page episodes (545 actions), and 10,000 full-credit generated
public scripted episodes. The browser set has 20 episodes per task except
cube: six requested faces, one wrong submission and one timeout. Exact
evidence and source hashes are in `RESULTS.json`. These are not learned
policy scores or a claim of unrestricted browser-pointer parity.
