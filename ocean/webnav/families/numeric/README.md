# Numeric and arithmetic tasks

## 5.0 conversion qualification, 2026-09-24

Implementation sources are preserved from `5c`. The capped build passes all
32 laws and the native regression suite. Fresh comparisons against pinned
MiniWoB revision `33c3b4ddef8c6eb67c57a29663d844b1eda7e614` pass 20 browser
episodes per task (180 total). The public-only scripted controller solves
1,000 generated episodes per task (9,000 total), including hot/cold using only
public feedback. `RESULTS.json` retains the historical audit. These are bounded
conformance/solvability checks, not PPO learning or full browser parity.

Nine MiniWoB tasks share a CPU Bend model: `ascending-numbers`,
`find-greatest`, `generate-number`, `guess-number`, `hot-cold`,
`number-checkboxes`, `odd-or-even`, `simple-algebra`, and
`simple-arithmetic`. `Model.bend` owns transitions, feedback and reward
timing. `Generate.bend` owns the bounded training presets. `Wire.bend` packs
the family state, while the C bridge and public API handle row transport,
action validation and projection.

```sh
make -f ocean/webnav/families/Makefile test FAMILY=numeric EPISODES=1000
make -f ocean/webnav/families/Makefile browser FAMILY=numeric EPISODES=20
```

`test_numeric.c` covers eight independent lanes, hidden-target noninterference,
input and checkbox actions, generated success paths, nonterminal guess
feedback, and reward deadlines. `browser_oracle.c` imports instances from the
pinned original HTML tasks, drives their controls, and compares public nodes,
feedback, terminal state and reward after each action. The harness controls the
clock; for `generate-number` it forces each original button draw to the
candidate produced by the Bend transition so both implementations receive the
same action result.

The Bend generators use compact deterministic distributions rather than
reproducing every original random draw and layout. They preserve each task's
public controls, solution structure, answer checking and reward behavior.
Original-browser coverage uses generated source instances, selected control
clicks and bounded action traces; arbitrary pointer geometry, complete
randomization parity, full DOM/AX output and the existing twelve-task PPO
profile remain outside this family.

The family uses eight 4096-word lanes. Task metadata occupies words 32–44;
word 34 carries the ascending cursor or the last submitted guess for feedback.
up to 32 nodes use 48 words each starting at 64; instruction and inserted
text are ASCII buffers at 1600 and 1856. Private answers and node targets stay
out of `WFView`. Input text uses the shared `MiniWoB/Text.bend` field model and
controls use `MiniWoB/Widgets.bend` nodes.

Validated results are recorded in [RESULTS.json](RESULTS.json): 32 checked
Bend laws, native regressions, 180 matched original-browser episodes and
9,000/9,000 public-observation scripted episodes. These are conformance and
solvability checks, not learned-agent performance. The scripted hot/cold solver
retains only candidate locations inferred from public temperature feedback;
the checkbox solver knows the ten fixed public reference glyphs. It does not
read per-episode target flags, and this is not a general visual encoder.

Run one task with action tracing after building the shared runner:

```sh
build/webnav/families/runner build/webnav/families/numeric/libnumeric.so 10 1 hot-cold
```
