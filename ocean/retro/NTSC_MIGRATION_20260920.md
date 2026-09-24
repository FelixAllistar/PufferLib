# NTSC ROM migration — 2026-09-20

## Imported cartridge and identity

Imported the user's `Super Mario Bros. (World).nes` from their Windows
Downloads ZIP into `ocean/retro/roms/smb1_ntsc.nes`, without changing any bytes.
The new local ROM is git-ignored; the original PAL `smb1.nes` is untouched.

- Full NES 2.0 image SHA-1: `33d23c2f2cfa4c9efec87f7bc1321ce3ce6c89bd`
- PRG+CHR SHA-256: `fcb6a0ef3a20c19b356005fbb21dc8009563b1cb5a9aaebc8e9386b4a8c5912e`
- PRG FNV: `96ec527fbb9bd9ca`
- Replacing only the container header in memory with the canonical iNES
  header produces SHA-1 `ea343f4e445a9050d4b4fbac2c77d0693b1d0922`, the
  [standard NTSC World image](https://tasvideos.org/1G#GameVersions).

The environment and CPU-block generator share `retro_rom_identity.h` and
accept only these two verified NTSC wrappers. PAL and changed images are
rejected. Level tables at $9CB4/$9CBC/$9D28/$9D2C/$9D4E match the imported
NTSC PRG; all 32 prepared starts validate their loaded data pointers.
The specialized CPU was regenerated from this PRG (10,236 instruction entries).

## Intro skip: retained, with a cold-boot equivalence test

For 1-1, no RAM writes are used to select the start. The ROM boots, START is
pressed normally, and the intro is emulated once. We save at the
[autosplitter's start event](https://github.com/periwinkle9/smb-autosplitter/blob/main/SuperMarioBros.asl):
entry into player control. The clock then starts at zero on each restoration.

On this fixed boot/title-input sequence, that state is at core frame 194,
162 frames after START, with HUD TIME 400 and Mario X=40. The saved start,
CPU/PPU/APU state, RAM/RNG, and image match an independently executed normal
new game byte-for-byte. The next 168 idle frames also match exactly. No
timed frames are skipped; no guessed title-screen offset is added.

The strict comparison exposed three uninitialized *reserved* PPU snapshot
bytes. Initializing those bytes removes serialization nondeterminism without
changing emulated hardware state or gameplay.

This is a fixed, naturally reachable title-screen phase, not a reproduction
of an arbitrary runner's title delay/RNG manipulation. Later-level selected
starts remain practice segments, not full-game runs from 1-1.

## Timers and reward

- Verified active HUD countdown: one unit per 24 emulated frames.
- Verified interval counter: one cycle per 21 emulated frames.
- Frame-derived NTSC RTA: frames / 60.0988138974405.
- Completion reward and viewer/dashboard completion use the same black-screen
  split; title time is excluded, while in-run pipes/countdown/fireworks count.
- Completed-only averages are again `rta_seconds`, `rta_frames`,
  `rta_clear_rate`; `rta_valid=1` identifies the matched ROM/core, not human
  leaderboard eligibility. Host rendering/inference pauses are not RTA time.
- Reward coefficients and learner settings were not changed during migration.

## Existing-policy transfer check

Pinned latest checkpoint:
`checkpoints/retro_cnn_64x60_speed/retro/1789899311557/0000000149946368.bin`

Its deterministic NTSC replay reaches the underground at frame 545, exits
to the overworld at frame 1006, then enters the death routine at frame 1094,
X=2717, HUD TIME 370. An eight-attempt sampled panel also had 0/8 clears.
The earlier 115M-step checkpoint similarly dies after the exit. These
PAL-trained weights remain loadable but need NTSC adaptation; this is not
evidence of an incorrect elapsed-frame clock. No long training was started
and no existing checkpoint was modified.

## Verification

- Full native `puffer` and standalone `retro` builds completed.
- All 32 starts and cold-boot equivalence tests passed.
- 65,536 reference/full/skip-render frames passed.
- 65,936 compiled/reference ROM frames passed, plus 162,096 instruction
  edge cases, 1,024 self-modifying RAM instructions, and 8,192 poll cases.
- Vector, observation, natural-life playback, and headless viewer CLI tests passed.
- PAL input was explicitly rejected by the new block generator.
- A real NTSC finish was verified using the learned policy through both
  pipes plus an explicit scripted jumping continuation. The finish occurred
  at 2050 frames / 34.110490 seconds, and terminal reward, retained viewer
  split, completed-only log, and independent video-frame count all agreed.
  This is a regression fixture, **not** a successful policy evaluation.
- A separate 24,576-step native GPU smoke run with learning rate zero wrote
  only to `/tmp/retro-ntsc-smoke.9qKetV`. It correctly recorded no successful
  clears, zero completed-only means, `rta_clear_rate=0` and `rta_valid=1`.
  The imported ROM, source checkpoints and learner settings were unchanged.

The separate FCEUmm cold-boot probe remains a diagnostic, not a hardware
equivalence certificate: boot/RNG differences yield full-RAM mismatches.
It is not counted as a passing parity test.
