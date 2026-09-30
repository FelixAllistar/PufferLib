# Composite form tasks

This family implements local task IDs 0–4 for `form-sequence`,
`form-sequence-2`, `form-sequence-3`, `multi-layouts`, and `multi-orderings`.
Root qualified all five tasks on 2026-09-29: ten Bend laws, 768 successful
native cases plus independent failure/clock/private-view checks, 640 full-row
fresh-versus-dirty reset comparisons, 100 original-page episodes, and
5,000/5,000 public scripted successes. `RESULTS.json` records commands and
source hashes. The shared registry is maintained by the integrating agent.

## Pinned source audit

Sources are the five corresponding HTML files under
`build/webnav/reference/MiniWoB-plusplus-33c3b4ddef8c6eb67c57a29663d844b1eda7e614/miniwob/html/miniwob/`,
plus `html/core/core.js` and `html/common/ui_utils.js`.

| ID | Page | Generation and behavior | Reward and time |
|---:|---|---|---|
| 0 | `form-sequence` | Slider range −10…10 inclusive, unit steps, randomized initial value and width 50…114 CSS pixels. The source samples an orientation but always creates a horizontal slider. Three checkboxes can be toggled independently. | Submit succeeds iff the slider equals the requested value and exactly the requested checkbox is checked. 10 s deadline. Correct reward is time scaled; wrong Submit and timeout return −1. |
| 1 | `form-sequence-2` | Three radios, three textboxes, requested radio 1…3, number −10…50, textbox 1…3. | The code tests whether **any** radio is selected, despite naming a requested radio. Only the requested textbox may contain the exact decimal number; the other two must be empty. 10 s; correct reward is time scaled. |
| 2 | `form-sequence-3` | Six nonblank dropdown choices exist, but the source samples from `SELECT_OPTS.slice(1, -1)`, so the requested value is one of the first five nonblank choices and never `6ft 2in`. The action button is Yes, No, or Maybe. | Clicking a button ends the episode. Both dropdown selection and button text must match. 10 s; correct reward is time scaled. |
| 3 | `multi-layouts` | Genre sampled from 18 labels then lowercased, director from `ui_utils.LAST_NAMES`, year 1970…2017. One of five layouts is sampled. The three field rows are shuffled in each. Button label is Submit, Search, or Go! according to layout. | Genre and director compare after lowercase and trim; year compares as exact decimal string. 20 s. `core.endEpisode(1.0, true)` scales correct reward. |
| 4 | `multi-orderings` | Same genre, director, and year generation, fixed table layout with three shuffled field rows. | Same field comparison and 20 s reward as ID 3. |

The shared `core.endEpisode` stores raw reward and applies
`max(0, 1 − elapsed/deadline)` only when the page requests time scaling.
Timeouts end with raw and reported reward −1. Source `randi` upper bounds
are exclusive; the source's slider, text number, and movie year bounds above
account for that. The source does not require users to perform actions in the
order described by the query. State transitions therefore depend on current
widget values at Submit, not the action history.

## Transport and observation

ABI v2 uses 8192 U32 words per row and four lanes. Header offsets are the
common `family_api.h` offsets. Offset 32 is current slider offset, selected
radio, or dropdown index; 33 is the ID 0 checkbox bitmask; 34 and 35 are
private requested controls; 36 and 37 hold public layout and order metadata
for movie forms. Current text lengths are at 40–42 with text at
512+128×field; private goal lengths are at 44–46 with goals at
1024+128×field. The public instruction begins at 128. A separate payload
begins at 2048. `WF_INSERT` atomically replaces a field's value; original
browser text entry is compared after that assignment. Public views omit
offsets 34–35 and all private goal slots.

ID 0 uses `WF_SELECT_OPTION` on ref 1 to set slider offset 0…20;
`WF_CLICK` refs 2–4 toggle checkboxes, ref 5 submits. ID 1 uses click refs
1–3 for radios, insert refs 4–6 for textboxes, click ref 7 to submit. ID 2
uses select ref 1 with choice 0…6 and click refs 2–4 for Yes, No, Maybe.
IDs 3–4 use insert refs 1–3 for genre, director and year; click ref 4 to
submit. Source generated movie directors cover a documented 20-name subset
of the original `ui_utils.LAST_NAMES` table. Original browser instances from
the full table use the same imported transition model.

`browser_oracle.c` imports each original instance once, then compares
independently advanced browser and Bend states after every action. It does
not overwrite model state from browser snapshots after actions. Its widget
adapter uses jQuery's slider/select controls and DOM input assignment;
coordinate gestures, animation geometry and individual keyboard editing
remain outside this atomic control preset. The public controller reads only
`WFView` instructions and visible node values.

These are behavior and scripted-solvability results. Full browser parity and
learned PPO performance remain unverified.
