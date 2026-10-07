# Native engine daylight captures, 2026-10-07

These are actual Windows GTX 1060 captures from `swat_test_environment_art`,
960 × 800. They use the existing Room 101 v2 art and the new CC0 HDR lighting.
The next interior/window/curtain art handoff is still in progress. Contact AO
is disabled, matching the default player after performance checks.

- `entrance.png`: camera position (-6.7, 1.64, 4.2), target (-6.95, 1.26, 0),
  vertical FOV 45°. The authored door is closed for comparison.
- `interior.png`: camera position (-7.2, 1.62, -2.1), target (-6.45, 1.22, -5.2),
  vertical FOV 64°. Interior ambient still uses the bounded room approximation.
- `player.png`: 1440 × 810 native player launch capture, using
  `./swat play --mission motel --capture ...`; includes the verified visible HDR sky.

The matched room views use Y-up, camera up (0, 1, 0), exposure 1.1. The prior renderer's entrance
capture remains at `../room101-after.png`. These captures are renderer evidence,
not a replacement for the artist's Blender source or movement/damage proofs.
