# Geometry source and bounds audit

Local IDs 0–4 map to `bisect-angle`, `circle-center`, `find-midpoint`,
`grid-coordinate`, and `right-angle`. The pinned originals are those pages
under `build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/`,
with `html/core/core.js`, D3 v3, and `html/common/shapes.js`. All five use
core's default 10,000 ms deadline. Success or an accepted partial score is
multiplied by `max(0,1−elapsed/10000)`; misses and timeout yield unscaled −1.

Grid-coordinate draws 25 r4 circles in column-major loop order. Circle
index `5*column+row` is labeled `(column−2,2−row)` and centered at
`(30*column+15,30*row+15)`. The query publicly names one coordinate. Its
circle ends with +1; every other circle ends with −1. Bend generates the
varying query goal and judges the clicked circle.

The other pages draw into a 150×130 SVG below a 50 px query. Their click
handlers use `event.pageX` as SVG `cx` and `event.pageY−50` as SVG `cy`,
without subtracting the SVG's own padding. The ABI pointer action transports
coordinates in 1/256 px units, but pinned Chromium truncates fractional
`pageX/pageY` to integer pixels before the handler. Bend floors the transported
coordinates to match. The public scene shows the source
circle geometry in SVG coordinates and the latest user point, plus Submit.
The browser fixture dispatches real CDP mouse events at those page positions.

- Circle-center chooses radius 25–39, then each center coordinate from
  `5+r` through `129−r`. Source scoring compares the user r3.5 circle's
  `getBBox` origin to `(cx−3,cy−3)`: the mathematical optimum is
  `(cx+0.5,cy+0.5)`, which integer page clicks cannot attain. Distance
  strictly below the generated radius earns
  `(r−distance)/r`; otherwise the result is −1. Missing Submit also gives −1.
- Find-midpoint chooses a first center x5–134/y10–119 and a second
  x5–134/y10–124, rejecting pairs unless at least one axis differs by
  30 or more. Both circles have r3.5, so their equal bounding-box offsets
  cancel. User distance to the mean center must be strictly below 30;
  accepted raw reward is `(30−distance)/30`.
- Right-angle uses the same initial bounds and 30 px spacing. The initial
  circles have r3.5, but the user circle has r3. The angle's effective user
  point is the clicked SVG center plus 0.5 in both axes. The source uses
  law-of-cosines on `getBBox` origins. If `abs(90−angle)<45`, raw reward is
  `(90−abs(90−angle))/90`; otherwise it is −1.
- Bisect-angle chooses three centers x5–134/y10–119, rejecting each new
  point when both axes are less than 40 px from any prior point. The first
  blue center is the vertex; two black centers are endpoints. The source
  draws a green endpoint midpoint on Submit, but scores the actual
  law-of-cosines split. Let `T` be the angle between endpoints and `A`,`B`
  the two angles through the user's point. If either exceeds `T`, result
  is −1; otherwise raw reward is `(T−max(A,B))/(T/2)`, including negative
  values that the source still time-scales.

The 2048-word ABI-v2 row has standard header fields 0–13, grid goal and
selected index at 32–33, drawing initial centers/radius at 40–46, user
coordinates/visibility at 47–49, and the source query at 512. Bend owns
generation, all task transitions, score, deadline and absorption. The C
adapter validates transport and projects the public DOM-like scene; it does
not calculate outcomes. The public controller reads only WFView geometry and
instruction. The native score oracle uses independent C double arithmetic;
the Bend model uses stock F32 operations, so comparisons allow 2e−4 for
generated cases and 5e−4 against the original browser.

The seeded Bend generator draws varied points/radii within the source bounds
and spacing constraints. It deliberately chooses a solvable subset of the
original positions for the public policy; it does not claim the original
random distribution. The browser fixture captures the pinned page's original
visible circles and query **only at reset**, then lets the page's event
handlers process every subsequent pointer click and Submit. It never
overwrites model state from later browser snapshots. Dirty resets compare
entire rows; changing the grid's private duplicate goal field does not alter
its public query or circles. The first grid increment passed root's guarded
proof/runtime/native gate. All five tasks now pass ten laws, independent native
tests, 100 original-page episodes (280 comparisons), and 5,000 generated public
checks with raw reward at least 0.95. Of those, 1,560 reach the separate 0.999
full-credit threshold; the rest retain their actual partial scores. Per-task
means range from 0.977455 to 1.0. See [RESULTS.json](RESULTS.json) for exact
scores, commands and source fingerprints. No RL training was run for this family.
