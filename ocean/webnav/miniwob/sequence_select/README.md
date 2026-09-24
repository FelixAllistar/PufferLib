# MiniWoB sequence and choose-list lane

This directory contains the stock CPU Bend model for the pinned
`click-button-sequence` and `choose-list` pages. `Model.bend` is the pure task
semantics; `LAWS.bend` and `PROOF.bend` state and check the transition laws;
`Wire.bend` is the 256-word transport used by the shared webnav dispatcher.

The sequence page draws each button's left coordinate from `randi(0, 118)` and
draws its top coordinate independently from `randi(0, 118) + 50`. Button
rectangles are 40 by 40 and use half-open bounds. The second button is later
in the HTML, so a coordinate inside both rectangles is dispatched to the second
button. Background clicks
are misses and leave the task running. Only the second actual button event
ends the episode: `one` followed by `two` succeeds, and every other pair
fails. A wrong first event therefore remains observable as a running state.

The choose-list model stores the visible option labels and the target label.
There are 3 through 9 options, with selection initially at index zero.
`Select(index)` has last-write-wins behavior for valid indices and is an
identity for invalid indices. `Submit` compares the selected visible text to
the target visible text, so duplicate labels are accepted. `Reset` restores
the initial selection and running outcome while preserving the generated
options and target. Both tasks absorb ordinary actions after termination and
time out at elapsed time 10000; reset remains available after termination and
at the deadline.

The private row ABI is deliberately local to this lane. Each row is 256
`U32` words and the batch has 32 rows:

| words | sequence, tag 7 | choose-list, tag 8 |
| --- | --- | --- |
| 0 | tag | tag |
| 1..9 | status, success, command, args 4/5, elapsed, saved elapsed, raw and timed reward bits | status, success, command, index, unused 5, elapsed, saved elapsed, raw and timed reward bits |
| 10..12 | actual click count, first hit, last observed hit | selected index, target index, version |
| 13..17 | first and second rectangle left/top, version 1 | option count at 13; option lengths 17 through 25 |
| 32..175 | unused | nine 16-word little-endian ASCII label slots |

Sequence commands are wait `0`, coordinate click `1`, timeout `3`, and reset
`4`. Choose-list commands are wait `0`, select `1`, submit `2`, timeout `3`,
and reset `4`. Hit codes are miss `0`, first button `1`, and second button
`2`. A reset clears status, saved timing, rewards, click history, and the
last-hit field while retaining the generated rectangles or labels.

`validation.h` checks tags, command/status ranges, the source rectangle bounds,
monotone timing for non-reset commands, the sequence history invariants, and
the choose-list count/index/version/ASCII packing invariants. It rejects
unknown tags. The packed option slots use printable ASCII bytes and require
zero padding after each declared length.

The current DOM-v2 observation schema does not expose native `OPTION` nodes
because their bounding rectangles are zero and the role extractor does not
assign them a native option role. The model therefore treats option labels as
the task's public text state, while a browser oracle must compare
`select.options` separately. This directory does not change that shared
visibility contract.

Run the local checks with:

```sh
ocean/webnav/miniwob/sequence_select/build.sh
```

The script checks Bend 2.0.6, runs the model proofs and wire checker, emits a
local `Main.bend` translation under `build/webnav/sequence_select`, and links
`test_native.c` against the shared compiled bridge object. The native test
keeps an independent C reference transition function and differentially checks
all 32 lanes, including overlap dispatch, deadline/reset behavior, duplicate
visible labels, invalid selection identity, and reward fields. The shared
root integration still owns routing and the browser CDP oracle.
