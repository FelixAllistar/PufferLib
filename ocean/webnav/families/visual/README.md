# Visual family source audit and bounds

The local IDs are 0 `click-color`, 1 `click-pie`, 2 `click-pie-nodelay`,
3 `click-shades`, 4 `click-shape`,
5 `count-shape`, 6 `count-sides`, 7 `identify-shape`, and 8 `visual-addition`.
All pinned originals are under
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/`.

Click-color shuffles 13 CSS colors, displays four distinct boxes, and samples
one of those as the target. In roughly 40% of instances the query names that
color; otherwise it renders a small colored query DIV. Source code gives the
query DIV class `color` and a click handler, but no `data-color`, so clicking
it fails. The four option boxes' CSS colors and `data-color` attributes are
public; the projection exposes them as scene values. The query swatch color
is likewise public. Bend owns generation, click outcome, deadline and reward.
The generator uses a varied seeded distinct-color preset; it does not claim
the original shuffle distribution.

## Pinned source behavior and implemented presets

Both pie pages shuffle the 62 distinct characters from
`ui_utils.alphanumericChars`, take 4–8 labels, and require clicking the
spreader before selecting a wheel item. The standard page inherits
wheelnav's 1500 ms animation; `click-pie-nodelay` sets animation time zero
and linear easing. The label is public in the query, but options should only
enter the visible scene when the wheel opens. The source schedules removal
50 ms after a choice; core has already ended that episode. The standard
page's 1500 ms wait is a settled animation preset. Intermediate wheel
hit testing and exact visual interpolation are outside this model.

Click-shades overrides the deadline to 15,000 ms. It creates 12 HSL spans,
with hue 0/red, 120/green or 240/blue; the first two shuffled hue groups
each receive 3–5 spans and the last receives the remainder. Each click
toggles the `selected` class. Submit succeeds only if *every* selected
span has the query hue and the selected count equals the generated count.
Both the HSL CSS color and selected border are visible; the answer bitset
must not be projected.

Click-shape uses `shapes.genGrid`'s 3–19 distinct cells and the source
`generalDesc` predicate. A clicked SVG item succeeds when its size/color/
glyph-or-type matches the query; multiple items may match. A blank SVG
click fails. Count-shape changes the grid's y-bin count to six and maximum
shape count to ten. It samples an unrelated descriptor in 30% of episodes,
so zero matches are valid. It displays five distinct numeric buttons, one
with the actual count and four distractors; a blank SVG click fails. The
public SVG tag, glyph, fill and size recover the same item attributes the
source predicate uses. The query descriptor can be parsed back into its
optional small/large and color words plus final glyph/type/item token.

Count-sides resets a 150×100 canvas, rotates 0–180 degrees and outlines
a regular 3–7-sided polygon of radius 35. The five buttons are 3–7.
The Bend generator uses a zero-degree rotation preset and projects the
polygon vertices as a public canvas outline. Source rotation variation,
antialiasing, and exact pixel coverage remain outside this preset.
Identify-shape renders one SVG letter, digit, circle, rectangle or triangle
and offers five fixed category buttons. Its `drawShapes(grid)` discards the
input argument and regenerates the visible shape; the rendered item is the
instance to judge. Visual-addition shows 1–10 blue blocks on each side,
uses a 15,000 ms deadline, and compares the textbox's exact string with
the sum's ordinary decimal spelling. Leading zeroes and whitespace fail.
The ABI accepts printable ASCII input up to 64 bytes.

`public_controller.h` uses only `WFView` instructions and nodes. The shape
projection describes public SVG tags, text glyphs, fill colors, size and
position. The canvas projection describes its public outline; the addition
projection has one blue-block node per visible DOM span. Private answer
fields remain in the Bend row and are not projected as nodes.

The generator preserves task bounds and answer rules, but uses deterministic
seeded presets instead of browser random draws. Shape items occupy distinct
grid cells; count-shape can generate zero matches. Proof, runtime, native and
browser status should be reported separately after the integrating agent runs
each guarded check.

Qualification on 2026-09-29 passed 26 laws, independent native checks,
180 original-page episodes (307 actions), and 9,000 full-credit generated
public scripted episodes. Exact evidence and source hashes are recorded in
`RESULTS.json`. These are not learned-policy results or pixel parity.
