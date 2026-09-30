# Calendar family source audit and bounds

Status: **bounded behavior checked on 2026-09-29**. Root ran the serialized,
6 GiB/no-swap stock CPU build: 18 Bend laws and native tests passed. Original
page comparisons passed for 20 episodes per task (100 total), including wrong
submissions, cancel and deadline paths. The public scripted controller solved
5,000/5,000 generated episodes. See [RESULTS.json](RESULTS.json) for evidence
and scope. No learned-policy result or full browser parity is claimed.

Task indices: 0 `choose-date`, 1 `choose-date-easy`, 2
`choose-date-medium`, 3 `choose-date-nodelay`, 4 `daily-calendar`. All five
pages set a 20,000 ms deadline. The four date tasks use a readonly jQuery UI
datepicker and an exact input string comparison on Submit. The original
`ui_utils.randomDate` samples a time from 2016-01-01 up to but excluding
2016-12-31 at midnight; its target date can therefore be no later than
December 30, although December 31 remains selectable in the widget;
easy limits the range to December, medium to November–December. They use
`toLocaleDateString('en-US', {day:'2-digit',month:'2-digit',year:'numeric'})`
for the instruction and the datepicker's standard `mm/dd/yy` display. Day
selection closes the popup and does not finish the episode. Navigation stays
inside the 2016 bounds. The nodelay page sets `showAnim:''`; the other three
leave the jQuery UI opening animation enabled. The browser fixture waits 250
ms after opening those three popups, without changing the episode clock, and
settles remaining animation before comparisons. It fixes the wall-date origin
at 2026-01-01, so the default 2016 picker opens at its December upper bound.

`daily-calendar` uses a scrollable 24-hour grid of 48 half-hour cells. The
source starts a drag on `mousedown` over a half-hour cell, redraws `#newEvent`
on each `mousemove` over a cell, and opens the name dialog only on `mouseup`
over that new event. `renderEvent` stores start as the starting cell index and
end as the last hovered index plus one. Backward drags are drawn with flipped
CSS geometry while retaining reversed data indices; they cannot satisfy the
positive duration check. A move to the immediately preceding cell produces
zero duration and no new event. Cancel removes the draft and dialog, allowing
another drag. Create compares the name case insensitively and ends the episode.

The requested duration is 1–3 half-hours: `core.randi(0,3)` is half-open.
The requested windows are `[16,24]`, `[24,32]`, and `[32,40]` in half-hour
indices. The source's `correctEventWindow` checks that the *end* is between
the window bounds (inclusive); it does not require the start to lie in the
window. The three random existing events are generated in source regions
starting at 12, 24 and 36. `renderEvent` omits `data-start` for those events,
so `eventsOverlap` compares against `NaN` and never detects overlap. This
family preserves that scoring behavior. `rewardEpisode` calls `endEpisode(-1)`
on failure and then calls `endEpisode(1,true)` unconditionally; the first call
clears `EP_TIMER` and shows the sync cover, so the second has no effect. Wins
are time scaled, failures and timeout remain -1.

The generator uses deterministic, independent content, layout and target
streams. It preserves the page's finite task structure and day bounds, not the
original JavaScript random sequence or full DOM geometry. `Generate.bend`
writes the instruction and targets. `Model.bend` owns selection, drag,
submission, timing and reward; `Wire.bend` implements the ABI row and text
input transitions. C transports actions and projects public observations.
`browser_oracle.c` imports matching original-page instances, then compares
public state and reward after source-page events. `test_calendar.c` covers
calendar bounds, native lanes, goals hidden from `WFView`, success, failure,
cancel, absorption and deadlines. Each side advances independently after the
original instance is imported once at reset.

ABI v2 row: 1024 words per lane, four lanes. Shared header words 0–13 are
unchanged. Words 32–43 hold displayed month, selected ordinal, popup flag,
private date goal, private daily window/duration/name, drag start/end, dialog
flag, input length, and scroll in half-hour units. Words 48–56 hold the three
public existing-event start/end/name triples; word 57 is selection state;
word 58 is the inserted command length. The typed event name is at 64–95,
incoming text at 128–159, and the Bend-generated instruction at 256–447.
Private fields never enter `WFView`; the visible instruction naturally states
the requested date or event.

Date refs: 1 input, 2 Submit, 3 previous month, 4 next month, 5–35 day
numbers, 36 month title. Daily refs: 1–48 half-hour cells, 49–51 existing
events, 52 draft, 53 name input, 54 Cancel, 55 Create, 56 scrollable area.
`WF_SCROLL` targets 56 and puts the absolute top half-hour index (0–40) in
`arg0`; projection maps it to `scroll_y = 20*arg0` pixels. A drag sends
`WF_POINTER_DOWN` on its starting cell ref, `WF_POINTER_MOVE` on the last
hovered cell ref, and `WF_POINTER_UP` on 52. Date and Create actions use
`WF_CLICK`. This ref contract models source handlers; the projected local
geometry is approximate and needs a CDP geometry audit before claiming
coordinate parity. Offscreen cells require scrolling. The dialog supports
ASCII insertion, backspace and select-all at the end of its input; arbitrary
caret movement, keyboard shortcuts and datepicker keyboard navigation are
outside the current bounded model.

The shared scripted runner dispatches `calendar_public_action`. Reproduce with
`node ocean/webnav/families/build.cjs calendar --test`, followed by
`make -f ocean/webnav/families/Makefile browser FAMILY=calendar EPISODES=20` and
`make -f ocean/webnav/families/Makefile public-check FAMILY=calendar EPISODES=1000`.
Compilation remains serialized and confined. The controller has state for one
episode at a time; it is not a concurrent policy implementation.
