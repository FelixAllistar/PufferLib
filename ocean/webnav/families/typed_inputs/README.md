# Typed native inputs

Registered local IDs are 0 `enter-date`, 1 `enter-time`, and 2
`unicode-test`. The stock CPU Bend modules own instance generation, state
transitions, native value normalization, clocks, and rewards. The ABI-v2 C
adapter transports actions, validates bounded rows, converts Unicode scalars
to UTF-8 for public `WFView`, and implements independent test oracles. There
is no private target field in the public view.

`enter-date` is the original native `<input type=date>` task. Its displayed
instruction is `MM/DD/YYYY`, while the control's value is ISO `YYYY-MM-DD`;
Submit succeeds only when rearranging that value matches the instruction.
`enter-time` displays the locale-formatted 12-hour time but compares the
native `<input type=time>` value to exact zero-padded 24-hour `HH:MM`. Both
use a 20-second deadline, with positive reward scaled by remaining time.
`WF_INSERT` on ref 1 means **atomic native control value assignment**, not
raw keyboard events. Bend normalizes malformed or impossible dates/times to
empty. The transport caps ASCII assignments at 10 or 5 bytes respectively;
longer commands are rejected rather than truncated. Ref 2 submits and ref 1
may be focused without changing the value.

`unicode-test` has six visible slots, each a text div, distractor input, or
button. Its query quotes one of six UTF-8 labels: `ÖK`, `Cancél`, `♥♥♥`,
`确定`, `取消`, or `ヘルプ`. Clicking any button whose displayed label equals the
query succeeds, including duplicate-label buttons; other button clicks fail.
It has the source default 10-second deadline. The labels are stored as
Unicode scalar words in Bend rows and encoded into UTF-8 by the C public
projection. Distractor input typing is outside this bounded action preset;
button clicks and waits are supported. Slot geometry and randomly sampled
background words are approximate, while the button text, slot kinds, reward,
and deadline semantics are modeled. Each slot has a 64-scalar row buffer;
the pinned lorem vocabulary's longest word has 12 scalars, so its longest
three-word input label needs at most 41 scalars including spaces and colon.

Generated date goals vary by seed over a source-valid subset of 2010-2019:
every month and days 1-28. Generated time goals vary over valid hours and
minutes. Unicode generation makes six source-valid slots, guarantees one
button, and varies their kinds and labels. Fixed lorem distractor words are a
bounded source subset. Resets clear every 1024-word lane, including dirty
rows. Overflowing row text or unsupported command lengths are rejected.

The original-page differential fixture imports each page's **initial**
query, input value, Unicode slot text and kinds into an otherwise fresh row.
After that, browser and Bend advance independently and are compared after
every action. It presets `en-US` locale and `UTC` timezone because the
original time page calls `toLocaleTimeString()` without specifying either.
The fixture covers public-controller success, wrong submissions/clicks,
invalid native values, and deadlines. Native source tests cover normalization,
reward, reset from dirty rows, answer variation, public-view private-goal
noninterference, and public-view-only control.

Root's guarded qualification passed all 11 Bend laws, native regression and
public-controller checks, 60 original-page episodes, and 3,000/3,000 public
scripted generated episodes. The native tests include 12 dirty-row comparisons
and 77 private-goal noninterference checks across the three tasks. Evidence and
source hashes are in `RESULTS.json`. No learned-policy success or full
original-page parity is claimed.
