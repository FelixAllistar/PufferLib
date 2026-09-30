# Scroll family source audit

Root qualified all four tasks on 2026-09-29: 17 Bend laws, independent native
regressions, 80 fresh-versus-dirty row comparisons and 20 private-view checks,
80 original-page episodes, and 4,000/4,000 public scripted successes. Exact
commands and hashes are in `RESULTS.json`. Registered local IDs are
0 `click-scroll-list`, 1 `scroll-text`,
2 `scroll-text-2`, and 3 `sign-agreement`.

`scroll-text-2` generates 50–149 lorem words and puts the textarea near its
middle, at `parseInt(scrollHeight / 2) - 25` pixels (subject to browser
clamping). It chooses bottom or top with `core.randi(0,2)`. Submit checks
`abs(offsetHeight + scrollTop - expectedHeight) < 10`, where expectedHeight is
`scrollHeight` for bottom and `offsetHeight` for top. Thus a scrollTop below 10
passes top, and one within 9 pixels of the browser's maximum passes bottom.
It has the default 10,000 ms deadline. The Bend generator varies word count
across 50–149, using a small vocabulary from the original lorem list and a
300/100-pixel height preset. The model distinguishes
`clientHeight` for scroll clamping from `offsetHeight` for the source's score.
Matched browser instances import measured heights, text, position and query.
Native generated instances cover the scoring rule, not the full text or
geometry distribution.

`click-scroll-list` has a 15,000 ms deadline. It shuffles either the people
names or countries list, renders 8–11 options in a multiple select, and asks
for one or two distinct option *values*. Submit succeeds exactly when the
number of selected options equals the requested count and every selected value
occurs in the requested values. Duplicate country labels exist in the source
list (`Congo`); values, not option indices, drive the source's membership test.
Scrolling a select changes visibility, while selecting changes the score.

The atomic option-property preset also models Chromium 153's persistent active
selection end: the first selected option becomes an internal anchor. Later
changed selections, including deselections, reveal that anchor. Assigning the
same selected value has no scroll effect. Native option height is measured at
reset, and browser snapshots wait for the queued scroll to settle. This rule
comes from the pinned browser's
[select implementation](https://github.com/chromium/chromium/blob/153.0.8010.52/third_party/blink/renderer/core/html/forms/select_type.cc#L1598)
and [option setter](https://github.com/chromium/chromium/blob/153.0.8010.52/third_party/blink/renderer/core/html/forms/html_option_element.cc#L356).
Mouse/keyboard multiselection has additional rules outside this preset.

`scroll-text` generates 20–149 lorem words. The instruction asks for the last
word, which the page derives by splitting on whitespace and stripping a period
from that final word. On Submit it removes all nonalphanumeric characters from
the entered answer and compares the result case sensitively with the expected
word. It has the default 10,000 ms deadline. Its generated text, answer input,
selection/caret state, and the final word's location use bounded transport.
The Bend generator varies the final word by seed within the source vocabulary.

`sign-agreement` has a 10,000 ms deadline and three equiprobable modes. The
plain Cancel mode succeeds on Cancel immediately. The two named modes require
scrolling to the bottom, entering an exact name sampled from `FIFTY_NAMES`,
then clicking Agree or Cancel as instructed. Agree and the name input start
disabled. A textarea scroll event enables both when
`scrollHeight <= scrollTop + 85 + 10`; the Cancel button starts enabled even in
the named modes, and clicking it early fails there. After enabled, clicking the
wrong button fails. Name comparison is exact and case sensitive. The source
generates 150–299 lorem words in a disabled textarea; disabled still permits
scrolling. The threshold uses the hardcoded 85-pixel `HEIGHT`, not the actual
client height.

All four pages call `core.endEpisode`: successes get raw reward 1 and a time
scaled reward, failures and timeout get -1. `core.randi` has an exclusive upper
bound. Full source text distribution, arbitrary browser geometry, keyboard
interactions and complete browser parity remain unverified.

The expanded ABI row is 8192 words per lane, four lanes. Header words 0–13
follow `family_api.h`. Word 32 is `scrollTop`, 33 `scrollHeight`, 34
`clientHeight`, 35 the direction for task 2, 36–39 list count/selection/allowed
values/requested count, 40–41 agreement mode/enabled state, 42–46 text field
length/goal length/selection/incoming length, and 47 `offsetHeight`. Word 48
stores option height and 49 stores the one-based native selection anchor
(zero until first selection). The public
query starts at 256, up to 11 option names occupy 512–1279 with 64 words each,
textarea content starts at 1536 (3072 words), input value at 4608, private
target text at 4736, and incoming command text at 4864. Validation rejects
oversized content rather than truncating it.

`WF_SCROLL` targets ref 1 and uses an absolute pixel `scrollTop` in `arg0`.
List options have refs 2–12 and use `WF_SELECT_OPTION` with `arg0` 1 or 0;
Submit is ref 16. `scroll-text` has textarea ref 1, answer ref 2 and Submit
ref 3. `scroll-text-2` has textarea ref 1 and Submit ref 2. Agreement has
textarea ref 1, name ref 2, Cancel ref 3 and Agree ref 4. The public view
exposes instructions, visible text, option values and scroll bounds. It does
not expose private masks or target text apart from what the instruction and
visible textarea already state. Every public name and value has an initialized
`WFText`, including empty button and select values.

`browser_oracle.c` imports exact source instances at reset and compares later
position, selection or input, agreement enablement, terminal status and reward
after each independent action. Its scroll and select actions mutate original
DOM controls and dispatch source events; click handlers are invoked on the
original buttons. `public_controller.h` reads only `WFView` and offers a
single-episode scripted policy integrated into the shared runner. Offscreen
public options remain in `WFView` with `WF_VISIBLE` cleared; the action adapter
requires scrolling them into view before selection. The shared loader accepts
these public nodes while retaining its text, reference and geometry checks.
These are scripted behavior checks, not learned PPO results.
