# Forms family

Eight MiniWoB form tasks share one stock CPU Bend state machine, text editor,
clipboard, deadline and reward model:
`enter-text-dynamic`, `enter-text-2`, `enter-password`, `text-transform`,
`copy-paste`, `copy-paste-2`, `read-table-2`, and `login-user-popup`.

`Model.bend` owns focus, selection, edits, clipboard operations, submission,
popup transitions and terminal rewards. It uses the typed `Text.Field` editor
from `miniwob/Text.bend`. `Generate.bend` builds bounded ASCII instances;
`Wire.bend` is the only task/runtime bridge and serializes the typed state.
`LAWS.bend` and `PROOF.bend` cover terminal absorption, deadline priority,
popup cancel and popup failure. `family_api.c` only validates and packs the
public v2 transport. `bridge.c` adapts the common batched runtime. The native
test exercises every task, copy/paste, exact entry, popup outcomes, deadlines,
private-goal blindness and terminal stability.

```sh
make -f ocean/webnav/families/Makefile test FAMILY=forms EPISODES=1000
make -f ocean/webnav/families/Makefile browser FAMILY=forms EPISODES=20
```

The browser oracle imports seeded instances from the pinned original HTML,
drives controls with CDP, freezes the page clock, and compares public fields,
selection, enabled state, popup visibility, elapsed time and rewards after each
scripted episode. It includes successful text entry and clipboard traces,
popup cancel/OK branches, and deadline failures. The harness is a sampled
behavior comparison; it does not claim to reproduce the full MiniWoB source
distribution or every browser editing and clipboard edge case.

Transport uses four lanes of 8192 words. The common 32-word header is followed
by task metadata at words 32..38, up to four private field records at 128
(544 words each), static public nodes at 2400 (128 words each), the instruction
at 4608, clipboard data at 5120 and edit payload at 5376. Public observations
contain current field values, names, focus, selection, visible static controls,
the instruction and deadline; goals, popup trigger mode and clipboard contents
stay private to the transition model.

Deadlines follow the pinned pages: 10 seconds for dynamic text, case entry and
copy tasks; 15 seconds for password, text-transform and popup login; 20 seconds
for read-table-2. Successful single-target tasks use MiniWoB's time-shaped
reward. Password, table and login require every requested field. Login popup
cancel restores the form; OK ends with failure. The finite table and text
generators are deliberately bounded CPU fixtures rather than full replicas of
all original generator choices.
