# Full-game reconstruction feasibility — 2026-10-03

This is the historical feasibility report. The subsequent optimized runtime,
generator, fresh training integration and throughput measurements are documented
in the [README](README.md#runtime-use) and
[runtime evidence](results/runtime_20261003/qualification/summary.json).

The reference prototype reproduces **15,612 cases and 1,624,560 frames with zero
CPU or CUDA mismatches**. Each scene is initialized once; subsequent frames run
from native state and controller inputs. All 2,048 game-RAM bytes, CPU registers,
frame phase and debugging observations are compared. Observation values are
bitwise exact on CUDA, with no tolerance.

This establishes a working route to faithful, configurable CPU/CUDA scenes.
The fast hand-written FPG environment remains qualified for its earlier bounded
scope. A complete production simulator, exhaustive interaction coverage and
whole-game training remain future work.

## What was built

The prototype mechanically translates the verified local SMB1 program into
compiled C++/CUDA control flow: 10,701 instruction sites, preserving byte
arithmetic, registers, stack, timers, random state and actor allocation. It
uses local ROM data and retains the original machine-state representation.
There is no opcode fetch/decode loop at frame time.

The scene reset and world-data setters are shared by imported reference clips
and constructed variations. Tested parameters include player form, position and
fractions; actor positions and timers; powerup type and emergence; invincibility;
difficulty; random state; resident tiles; enemy types and block contents in level
streams. World data stays fixed during each replay. Future spawns are computed
by the engine.

Some initial history still resides in a complete state template. Turning every
field into convenient parameters and migrating the old procedural generator to
this schema require further work. Clip reconstruction here starts from emulator
state and input records.

## Qualification panel

| Panel | Cases | Frames | CPU failures | CUDA failures |
| --- | ---: | ---: | ---: | ---: |
| Main: recorded starts, action branches and scene variations | 11,396 | 1,178,672 | 0 | 0 |
| Powerups, invincibility and Bowser difficulty variants | 72 | 17,280 | 0 | 0 |
| World-data variations across all 32 stage starts | 896 | 92,672 | 0 | 0 |
| Imports during interrupted updates and transitions | 3,248 | 335,936 | 0 | 0 |
| **Total** | **15,612** | **1,624,560** | **0** | **0** |

There are 8,880 one-frame branches and 6,732 persistent trials of 240 frames.
5,152 cases are explicitly marked constructed. The remaining 10,460 replay
unmodified reference states from the declared practice-start protocol. These
counts include correlated branches and repeated start points; they are a
development regression panel.

The main panel records 407 start points from two scripted exploration patterns
across all 32 stages and ROM-validated routes to Bowser, a vine, a powerup, and
the 1-1 pipe route. Another 116 start points exercise imports during updates or
scene transitions. Stages after 1-1 use a title-screen practice-start selector
and execute the ROM's setup. A complete-game playthrough is still unverified.

The world panel changes 257 enemy/block-content data entries before setup and
replay. These cases exercise the same configurable data in the reference and
native engines, including later parsing and spawning.

The corpus includes Piranha Plants; Hammer Bros and their hammers; both Bullet
Bill systems; swimming and flying Cheep-cheeps; Bloopers; Lakitu and Spinies;
Podoboos; short and long firebars; Bowser and his flames; all four powerup types;
player fireballs; vines; springs; and several platform controllers.
The [coverage ledger](coverage.json) records sampled actor/state evidence and
remaining gaps for each family.

The comparisons continue through death and transition frames. The panel
contains 317,216 frames outside ordinary gameplay and 2,578 non-idle CPU frame
boundaries. Game-mode/task changes, form changes, injury and star activation
are recorded separately from actor presence.

## Exactness and limits

- The verification observation is 2,048 floats: each RAM byte divided by 256.
  That mapping is exactly representable. It is a lossless debugging view;
  the policy observation and encoder remain undecided.
- Hardware timing includes instruction cycles, DMA, interrupts, lag, sprite
  overflow and PPU status/open-bus behavior. The sprite-0 timing model assumes
  the original game's fixed status-bar marker. Pixel and audio output are
  outside this comparison.
- CUDA runs persistent states independently, with both direct launches and
  CUDA graphs. Expected frames are read only for input and comparison. They
  never replace the evolving native state.
- Negative controls detect corrupted expected RAM in a graph replay, an invalid
  scene reset, a bad header and a truncated trace. CPU failures, debug filtering
  and empty panels cannot produce a valid qualification trace.
- Actor presence and observed state changes leave many interactions unqualified.
  Remaining targets include the unobserved firebar/platform variants, paired
  scale interactions, complete vine/sky-room routes, castle maze/pipe sequences,
  every ending sequence, and two-player input. The current input adapter drives
  controller one.
- The local trace schema checks version, structure sizes and source ROM identity.
  Other ROM revisions and modified graphics require separate qualification.

The generated GPU verification build disables device optimization to bound
compiler memory. It is unsuitable for drawing throughput conclusions. This work
contains no new training or learning ablation.

## Evidence and experiment reset

Compact summaries, per-case records, guard results and hashes are archived in
[results/feasibility_20261003](results/feasibility_20261003/manifest.json).
ROM-derived traces and generated program/module data remain local under
`build/mario_sim/qualification_v2/` and `build/mario_sim/`.
The [README](README.md) gives the reproducible gate command.

The inventory covers 50 mechanic families and 34 distinct area data sets, with
all 32 starting-area pointers checked by executing reference setup. The enemy
and item list was cross-checked against Nintendo's
[original manual](https://www.nintendo.co.jp/clv/manuals/en/pdf/CLV-P-NAAAE.pdf)
and [Mario history page](https://www.nintendo.com/jp/character/mario/en/history/smb/index.html).

The [experiment audit](EXPERIMENT_AUDIT.md) withdraws prior simulator-dependent
encoder, reward, curriculum and training-budget conclusions. Raw checkpoints,
measurements and recorded ROM outcomes remain preserved. This feasibility result
does not restore those conclusions or authorize continuing historical weights
under a changed observation contract. Subsequent runtime training starts fresh.
