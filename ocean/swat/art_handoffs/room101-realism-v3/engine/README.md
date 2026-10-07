# Native Windows engine review — 2026-10-07

Captured with the actual raylib renderer, HDR daylight, room shadow atlas and
contact AO disabled. V2 and v3 comparisons use identical lighting and cameras;
both have the corrected multi-room shadows. These are not Blender renders.

| Capture | Content |
| --- | --- |
| `room101-v2.png` / `room101-v3.png` | Matched entrance, v2 / v3 |
| `room101-interior.png` / `room101-v3-interior.png` | Matched bathroom view, v2 / v3 |
| `room101-v3-bed.png` | V3 linen, carpet and existing bed furniture |
| `room101-v3-window.png` | Curtains viewed from inside, opaque glazing |
| `motel-all-room-shadows.png` | Three open entrances from outside; persistent room shadows |
| `all-room-shadows.png` | Three-room synthetic lighting regression scene |
| `player.png` | Normal `./swat play --mission motel --capture ...` launch |

Inspection cameras use Y-up world coordinates, no image edits, no actors, and
the current authoritative geometry. Entrance/interior comparisons retain the
existing test door pose. The row, bed and window captures open each entrance
100 degrees from its closed hinge pose for inspection only.

| View | Position | Target | Vertical FOV | Pixels |
| --- | --- | --- | --- | --- |
| Entrance | -6.7, 1.64, 4.2 | -6.95, 1.26, 0 | 45 | 960×800 |
| Interior | -7.2, 1.62, -2.1 | -6.45, 1.22, -5.2 | 64 | 960×800 |
| Bed | -7.15, 1.62, -0.67 | -5.74, 1.05, -2.93 | 59.863 | 960×800 |
| Window | -6.35, 1.55, -2.6 | -5.45, 1.45, 0 | 45.781 | 960×800 |
| Row | -4, 1.64, 8 | -4, 1.35, -3 | 54 | 1440×810 |

Bed/window framing uses the supplied world camera vectors and FOV but the
engine test's 960×800 target, so its aspect ratio differs from the offline views.

## Validation

- Native Windows lighting and environment GPU tests pass; Linux lighting and
  environment tests also passed before the final capture/ownership additions.
- Each of three test rooms has measured lamp-shadow contrast. Camera movement
  across the row and 20 m away leaves the fixed inspection image unchanged.
- Unchanged room geometry reuses cached depth. Removing one blocker immediately
  rebuilds only its room. A moving silhouette appears, then clears with no trail.
- All ten v3 replacement meshes render within their original local envelopes;
  parent removal hides them, and cutaway hides the roof. Door overlays follow
  0/45/100-degree poses and disappear with the door. Rendering leaves the world
  byte-for-byte unchanged. Material AO/normal strengths and lifecycle pass.
- Normal Windows player launch and capture succeeded after rebuilding `./swat`.

## Performance sample

Native Windows, GTX 1060 3 GB, same motel indoor shooting run, seed 42,
30 warmup + 300 measured frames, audio off, 147 objects / 9 actors,
36 shots / 1 destroyed object. Command for each executable:
`swat_performance_tool.exe --motel --indoors --fire --hidden --frames 300`.

| Metric (ms) | Before (v2 + nearest-room shadows) | After (v3 + all-room shadows) |
| --- | --- | --- |
| Total mean | 28.054 | 30.623 |
| Total median | 24.440 | 27.770 |
| Total p95 | 47.542 | 49.687 |
| Total maximum | 213.708 | 138.669 |
| Simulation mean | 3.453 | 3.953 |
| Draw submission mean | 19.923 | 21.759 |

One sequential pair on a busy workstation; the +2.569 ms mean difference covers
both art and shadow changes and is not an isolated GPU measurement. It does not
establish a speedup. Character draws fell from 1,984 to 1,735 with room culling;
both runs prepared 1,320 poses. Full local logs are under
`build/swat/shadows-2026-10-07/` (not versioned).

## Remaining visual work

The linen, printed numerals and AC detail improve the room. The blanket still
looks padded, bed furniture remains simplified, and the original sink and
bathroom finish still need a coherent art pass. Interior curtain shading also
needs review in these captures. Keep the authored handoff pinned until the next
revision is ready; the current set is installed without changing collision or
visibility authority.
