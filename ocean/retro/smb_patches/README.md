# smb_patches

Build-time patches for the fetched smb-vanilla-port core (pinned in
`build.sh`, never vendored). Applied once each (per-patch
`$SMBV_DIR/.patched-<name>` sentinels); object rebuilds are stamp-gated on
patch contents.

## loop_caps.patch

Bound three data-driven parse loops that can spin forever on corrupted
area/enemy offsets (wrong-warp/garbage-area states):

- `ProcessAreaData` (`area.c`): `while(true)` area-object parser.
- Area parser task drain (`common.c`): `do/while(AreaParserTaskNum != 0)`.
- `ProcLoopCommand` (`common.c`): enemy-data loader whose u8 offset wraps
  every ~128 iterations.

A wedged loop runs inside a single `SMB_tick`, so no wrapper-level watchdog
can catch it: one stuck env blocks its OMP thread, the rollout barrier never
completes, and the trainer spins silently (observed twice at ~15M steps).
Each cap is orders of magnitude above normal-play counts and fires a tripwire
print naming the loop. Bench + chaos checksums are bit-identical with caps in
place, so exactness is preserved. If a tripwire ever fires in training, the
printed loop name localizes the trigger state — report it upstream.

## music_timeout.patch

Death completes when the death jingle ends (`EventMusicBuffer == 0`), but
note lengths advance ~10x too slowly without an audio backend, so the tune
effectively never ends: the corpse falls forever, each step bleeds −2.5 with
zero completions (confirmed via death probes: `evmus=01` stuck, yhigh
climbing, lives never decrementing). On hardware the jingle (~150f) always
ends long before the corpse falls to yhigh 5, so proceeding there is
behavior-identical while guaranteeing completion headless.

## music_reload_guard.patch

Bound the music-reload cycle. `HandleSquare2Music`'s terminator can reload
an event tune (`LoadEventMusic` → `FindEventMusicHeader` → `LoadHeader` →
`HandleSquare2Music` → ...), and in a corrupted music state (observed at
area reload: `eng=00/x=0` after transition interruptions) that cycle repeats
forever — tail-call optimisation compiles it into a flat infinite loop
burning CPU indefinitely. Confirmed via tick-watchdog spin stacks landing in
`FindEventMusicHeader` / `HandleSquare2Music`. Fix: `LoadHeader` nests at
most 8 deep (legit play nests ~2–3), beyond that drop to silence; and a
zero event queue falls back to silence instead of upstream's `abort()` (the
original game infinitely loops there — upstream faithfully turns that into a
process kill, which would take down the whole trainer). On hardware neither
path is reachable, so exactness is preserved where it matters.
