# Controls family: checked scope

Root validated six family-local task IDs on **2026-09-29** under the serialized
stock CPU build: 17 Bend laws, 592 native checks, 120 original-page episodes
(333 state/reward comparisons), and 6,000/6,000 public scripted episodes passed.
See [RESULTS.json](RESULTS.json). `choose-list` ports an existing legacy task;
the other five names add bounded behavior coverage. These checks do not
establish full browser parity or a learned-policy score.

| ID | Original page and behavior | Current source boundary |
| --- | --- | --- |
| 0 | `choose-list`: 3–9 shuffled person names or countries; one selected option, then Submit; exact label match; 10 s. | Bend selects and scores. The generator samples distinct entries from a 16-entry original-name or original-country subset; full original list distribution is pending. |
| 1 | `use-slider`: min sampled from −100, 0, 10, 100; span sampled from 5, 10, 50, 100; horizontal or vertical, integer step, independent initial and target; 10 s. | Bend clamps normalized pointer position, handles drag and arrows, and scores on Submit. Actual browser pixel geometry and jQuery handle offsets require conformance review. |
| 2 | `use-slider-2`: three independent horizontal 0–20 sliders; the whole combination is checked on Submit; 20 s. | Bend keeps three values and requires all three matches. |
| 3 | `use-spinner`: starts at 0; target is −10 through 9; spinner buttons move by one, keyboard `keydown` is prevented; 10 s. | Bend button transitions and negative values; C admits `WF_KEY_DOWN` as an event that Bend ignores. |
| 4 | `use-colorwheel`: one of 19 named colors, hex input initially `AB2567`; continuous RGB reward; 7 s. | Hex input editing and Submit are modeled. jscolor picker pointer/HSV, blur normalization, malformed-input behavior, popup projection, and color persistence across episodes remain gaps. |
| 5 | `use-colorwheel-2`: six independent random hexadecimal digits shown in a swatch; same input and reward; 7 s. | Swatch is public, with independent channel generation in Bend. The same picker and reset gaps apply. |

The pinned source is under
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/`.
The relevant dependencies are `core/jquery-ui/jquery-ui.min.js`,
`core/jscolor.min.js`, and `common/ui_utils.js`. The original color reward is
`1 - (|ΔR| + |ΔG| + |ΔB|)/765`, and `core.endEpisode` scales positive rewards by
`max(0, 1 - elapsed/deadline)`. Browser JavaScript computes this in double;
the Bend transport records F32. Browser comparisons should use a small numeric
tolerance and keep raw and timed rewards separate. A malformed hex input is
not equivalent yet: the current model gives it −1 on Submit, whereas the
original jscolor blur/validation path can normalize or prevent completion.
The original page also leaves its color input in place between episodes;
the current generator initializes each reset to `AB2567`.

`Model.bend` owns transitions, reward, and deadlines. `Generate.bend` owns
instances; separate draw streams choose bounds, initial values, targets,
option permutation, and RGB channels. `Wire.bend` decodes only the ABI command
and serializes model state. `bridge.c` is runtime transport. `family_api.c`
validates rows/actions and projects public observations. It never computes
task success or reward. The public view exposes instructions, current values,
widget roles, slider bounds/orientation, option labels, and the swatch. It
never projects private target fields, seed, status, or rewards. The task-5
swatch is deliberately public because the original page displays it.

The family uses ABI v2 without changing shared enum values. Its row has 2048
words and four lanes. Header words 0–13 retain their ABI meanings; word 14 is
the action-text length. Words 32–49 contain family state; 46 is packed desired
RGB. Query text starts at 512, color input at 768, action text at 800, and
choose-list options at 1024 in 64-word slots. The single slider stores signed
values as `actual + 100`; the spinner uses a U32 bit pattern interpreted as a
signed 32-bit integer, so its buttons are not artificially clamped. Public
values are signed decimal. For a
slider `WF_CLICK` or `WF_POINTER_DOWN` / `WF_POINTER_MOVE`, `arg0` is an integer
0–1000 fraction from the left or top edge of the track. Vertical values rise
from bottom to top. `WF_POINTER_UP` releases the drag. `WF_KEY_DOWN` has the
standard keycode in `arg0` (Home 36, End 35, arrows 37–40); `WF_KEY_UP` is
accepted as an inert release. Spinner `WF_CLICK` refs 1 and 2 are up/down;
ref 3 submits. `WF_SELECT_OPTION` targets select ref 1 and passes the zero-based
index in `arg0`. Color text edits target input ref 1; `WF_SELECT_RANGE` uses
`arg0`/`arg1` for start/end and `WF_INSERT` carries ASCII in `WFAction.text`.
Refs and coordinates are part of the public action contract, never secret
set-answer operations.

`LAWS.bend` / `PROOF.bend` cover pointer endpoints, clamping, encoded-zero
decrement, reversibility, wrong three-slider submission, invalid selection,
wait, timeout priority, terminal absorption, and integer RGB distances. Native
tests separately check floating-point endpoint and interior rewards, because
stock proof normalization leaves F32 operations opaque. The
`test_controls.c` independently parses public instructions and checks rewards,
bounds, keyboard restrictions, goal-field noninterference, and lane isolation.
The `browser_oracle.c` + `browser.js` extract instances from the pinned
original pages, drive their original DOM event handlers, import browser state
into the Bend wire format **once per episode before any action**, and compare
subsequent model states and rewards to fresh browser snapshots. The action
plan reads the original snapshot's query, options, widget bounds, and swatch;
it does not derive expected post-action values from the Bend row. That browser
test checks transitions, not the generator's distribution. Its color cases
use valid six-digit hex input only and dispatch an input event to jscolor;
malformed input, blur normalization, and the pointer picker are outside its
current conformance scope. Slider/spinner comparisons dispatch through the
original jQuery event handlers. Slider coordinates retain fractions to match
the normalized action contract; native MouseEvent construction rounds pixels
and caused a one-value discrepancy on a 100-step vertical slider. Native pixel
dispatch, handle offsets and long-press spinner acceleration need separate
conformance work. The spinner fixture releases on the button to stop repeat.

`public_controller.h` is a public-view-only scripted controller. For the hex
tasks, the caller passes phase 0, 1, then 2 to select, insert, and submit.
It demonstrates a path through the text input; it does not demonstrate picker
solvability. Reproduce with the guarded `build.cjs controls --test`, then
`make -f ocean/webnav/families/Makefile browser FAMILY=controls EPISODES=20` and
`make -f ocean/webnav/families/Makefile public-check FAMILY=controls EPISODES=1000`.
