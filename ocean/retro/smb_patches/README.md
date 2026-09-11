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

## header_parse_fix.patch

**Upstream bug** (affects hardware-faithfulness everywhere, not just
headless): `LoadHeader` read the music header table one byte early —
`header[tune-1]` instead of `header[tune]`, and every field offset one byte
short (`NoteLenLookupTblOfs = header[off]` instead of `header[off+1]`, etc.).
The 6502 ($F6F6) uses the caller's 1-based bit-scan result directly. Found
via the parity tool's death-jingle trace: the port's note counters ran
~150 frames vs hardware's 4-24 (the same melody shifted +0x80), and the
music data pointer was garbage ($A0FD vs $FB73). Verified against the ROM
bytes and QuickNES: after the fix the fast backend's death jingle is
note-for-note identical to hardware and completes in ~120 frames, which
also removes the feeding condition for the reload-cycle spin (the corrupt
pointer walked data with no terminator). `music_timeout.patch` is now a
near no-op (kept as belt), `music_reload_guard` stays as corrupt-state
insurance.

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
