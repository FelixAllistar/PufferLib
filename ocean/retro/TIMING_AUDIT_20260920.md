# Timing/reward audit — 2026-09-20

This records the initial PAL diagnosis. The subsequent user-supplied NTSC
migration is documented in [NTSC_MIGRATION_20260920.md](NTSC_MIGRATION_20260920.md).

## Root cause: wrong ROM region, not a frame-counter scale error

The three local `smb1.nes` copies all have SHA-1
`ab30029efec6ccfc5d65dfda7fbc6e6489a80805`:

- `pufferlib/ocean/retro/roms/smb1.nes` (active)
- `ocean/retro/roms/smb1.nes` (parent workspace)
- `ocean/retro/roms/roms/smb1.nes` (parent workspace)

[TASVideos' game versions](https://tasvideos.org/1G#GameVersions) identify
this as Super Mario Bros. Europe Rev A/PAL. The known NTSC World iNES
release instead has SHA-1 `ea343f4e445a9050d4b4fbac2c77d0693b1d0922`.
The environment and CPU-block generator explicitly allowlist the PAL
fingerprint `31d802e3779199da`, but the environment previously called it NTSC.

The emulator core schedules NTSC video frames; the timer divides by 60.0988.
This is PAL game code on an NTSC core, not a valid PAL or NTSC speedrun setup.
The versions also differ in game behavior, so changing the divisor to 50
would not establish comparability. A matching ROM/core needs validation.
No ROM files or existing checkpoints were changed.

## Reproduction before config changes

Pinned checkpoint:
`checkpoints/retro_cnn_64x60_speed/retro/1789899311557/0000000115343360.bin`

Deterministic actions, frameskip 1, selected start 1-1:

| Event | Emulated frames | HUD TIME |
| --- | ---: | ---: |
| Playable reset | 0 | 400 |
| Down-pipe entry | 393 | 381 |
| Underground playable | 471 | 380 |
| Side-pipe entry | 609 | 374 |
| Overworld playable | 864 | 373 |
| Flag contact | 1116 | 361 |
| Next-level number loaded | 1766 | 0 |
| Black-screen split | 1774 | 0 |
| Next-level playable / old reward endpoint | 2230 | 400 |

The old viewer called 1774 / 60.0988 = 29.518060 seconds "RTA".
It is only simulated time for this mismatched setup. The old reward used
2230 frames (37.105566 simulated seconds), a different endpoint.

## Reward issues and cleanup

- Both obsolete HUD bonuses had been reenabled at 1. The completion HUD
  target 365..369 was evaluated after next-level TIME reset to 400, so it
  always paid its full extra bonus on these clears. Both bonuses are now 0;
  their target options were removed from the current config.
- Generic novel-area logic did not pay a second HUD bonus for returning
  to the already-visited overworld. Dedicated frame-based pipe segments
  already handle both arrivals; their small weight remains 0.5.
- The completion frame bonus had been reduced from 30 to 1. It is restored
  to 30 with the existing 1200..3000 frame shaping window, not a TAS target.
- Completion now uses the same black-screen split event and frame count
  as the viewer/logs (`completion_on_rta_split=1`). The option name is
  historical; it does not make the ROM/core region mismatch valid.
- Point, coin, checkpoint, idle and generic area rewards remain zero.
  Sweep parameters no longer reenable those terms. Learner settings unchanged.
- Viewers now label seconds SIM with a region warning. Logs use
  `sim_seconds`, `split_frames`, `split_clear_rate`, and `rta_valid=0`.

## Checks and limitation

The pinned learned-policy test matched every pre-reset frame against the
unoptimized reference CPU (idle skipping disabled), including serialized
CPU/PPU/APU state. The wrapper frame total also matched the emulator video
scheduler's independent counter. The split remained exactly 1774 frames
after reward cleanup; terminal reward, viewer split and completed-only
metrics now agree. These checks establish internal consistency, NOT hardware
or NTSC-version equivalence.

Rebuilt both `./puffer` and `./retro`. ROM/unit tests and headless viewer CLI
tests passed. A separate 24,576-step native GPU smoke run with learning rate
zero wrote only to `/tmp/retro-rta-region-smoke.duwIu5`: its completed mean
was 1765 frames, `sim_seconds=clear_seconds=29.368307`, clear rate 1 and
`rta_valid=0`. Sampling makes this mean differ from the deterministic replay.
The user's existing training process was not stopped or restarted; it still
holds the old binary/config until restarted.

Next: supply a locally owned NTSC ROM, validate its identity and level tables,
regenerate specialized CPU blocks, rebuild, and remeasure. Existing policies
were trained on PAL game code; their performance on NTSC is not established.
