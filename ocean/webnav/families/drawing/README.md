# Drawing family

Local IDs are 0 `draw-circle` and 1 `draw-line`. Stock CPU Bend owns stroke
capture/replacement, pointer sampling, D3 basis-curve bounds, seeded dot and
orientation generation, deadline, raw reward and positive time scaling. C
validates/transports actions and projects public SVG geometry plus the agent's
own pointer history. This is a scene-and-input-history observation preset;
the original tasks do not provide a sufficient text-only accessibility view.
It is not a pixel policy or a claim of general DOM/AX rendering parity.

Both source pages use a 150×110 SVG and a 10-second deadline. Down starts a
new path, replacing the old stroke; moves append samples only while captured;
release ends capture. Submit with no path loses. The supported pointer domain
is the interior SVG rectangle, with at most 256 samples per stroke. Leaving
the rectangle, touch/multi-pointer input, and submit while the pointer remains
pressed are outside this preset. A new down while held is also rejected.
Coordinates use Q256 unsigned transport; Bend reproduces Chromium's integer
MouseEvent coordinate truncation in the tested viewport. Centers retain
fractional precision, stored as Q65536. Task generation uses varied integer
centers in the original bounds and both line orientations, with an independent
seeded stream rather than the original continuous random distribution.

`draw-line` has two source quirks. Its destination is four pixels up and left
of the visible dot center. Its `standardDevs` computes sums but returns only
the **last** sample's squared deviation divided by the sample count. The model
retains both. Direction is worth 80% and distance to the nearest sample 20%,
with the original strict thresholds. There is no required path length or
straightness check beyond that buggy calculation. Negative outcomes are not
time-scaled; positive outcomes are.

`draw-circle` measures radial-distance standard deviation around the rendered
dot center, then combines its banded reward with the SVG path's size. The
source first filters control points using a strict distance-squared >100 rule,
then renders `d3.curveBasis`. Thus raw sample bounds are insufficient and tiny
closed circles can collapse to a zero-size path. `Curve.bend` reproduces the
filter, cubic basis controls, and analytic cubic extrema. The implementation
uses F32; browser comparisons allow 0.003 pixels of bound error and 1e-5 reward
error. F64-equivalent behavior arbitrarily close to numerical thresholds is
not claimed.

Rows contain 2048 U32 words, four lanes: common header; public dot x/y at
32–33; private direction at 34; sample count/capture at 36–37; final path bounds
at 38–39; reversed sample pairs at 128; public instruction at 768. The view
contains canvas, Submit, and the colored dot. The canvas value contains the
current capture state and past input samples. Private direction, rewards and
seed are not projected; a noninterference test changes the private direction
while keeping the public instruction fixed.

Six laws cover capture/release, stroke replacement, deadline precedence and
terminal absorption. Independent native fixtures check original line scoring,
small-path size bands, and curve bounds obtained from the pinned D3 code,
alongside generated public solves and dirty reset comparisons. The browser
fixture uses the actual pinned original HTML and CDP mouse events. It imports
only the generated center/instruction at reset and compares subsequent samples,
capture, final SVG bounds, status and rewards without repairing model state.
Qualification passed on 2026-09-29: six laws, independent native fixtures,
40 original-page episodes (322 actions), and 2,000 generated public-controller
episodes, all with full credit. Original-page traces deliberately include
wrong submissions and timeouts. Exact evidence and source hashes are in
`RESULTS.json`; these are scripted checks, not learned-policy results.
