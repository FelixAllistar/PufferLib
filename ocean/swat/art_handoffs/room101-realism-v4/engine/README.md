# Native Windows review — 2026-10-07

Room 101 v4 is installed over v3. These captures come from the actual raylib
renderer on native Windows, with HDR daylight and contact AO disabled.

## Lamp coverage repair

The former downward 150-degree lamp shadow projection treated surfaces outside
its cone as fully lit. It caused the broad curtain band and the bright strip on
the bathroom back wall. Each room now has six 384-pixel views covering the whole
lamp, with two guard texels per edge and projection derivatives computed from
the receiver position before face selection can introduce a discontinuity.

The static and dynamic depth atlases are 2304×3072 each: 1.5 times the previous
lamp texel count, about 18 MiB more total depth storage. Static room geometry
remains cached; doors/destruction invalidate affected rooms immediately and
actors refresh every four simulation ticks. Room and face bounds cull casters.
This is still bounded room lighting, without physical fixture emission or GI.

The old renderer failed the side-facing blocker test with 0/6144 shadowed
receiver samples. The new renderer passes all six axis directions and the
+X/+Z seam with 6144/6144 each. Native Windows lighting tests also verify
camera-independent room shadows, local cache invalidation, moving silhouettes
without trails, transformed mesh/batch equivalence and immutable world state.

## Captures

- `room101-v4-{entrance,bathroom,bed,window,row}.png`: installed v4 art.
- `room101-v3-{window,bed,interior}.png`: v3 under the same corrected lighting.
- `room101-v4-window-no-lamp-shadow.png`: lamp receiving disabled, normals kept.
- `room101-v4-window-no-normal.png`: authored window normal strength set to zero,
  shadow receiving kept. Both diagnostics restore their settings afterward.
- `lamp-direction-6.png`: occluded receiver crossing a cube-face seam.
- `player.png`: normal launcher, native Windows player, motel scenario.

The broad curtain band is absent even on v3 under corrected lighting. The v4
normal-zero control leaves its broad shading intact. The old projection image
is preserved at `../../room101-realism-v3/engine/room101-v3-window.png`.
V4 changes the thin curtain rim geometry/UVs; it does not paint out the band.

Cameras match the previous native review's position, target and vertical FOV,
at 960×800. Entrances are open 100 degrees for the v4 inspection captures.
V3 interior was captured before that inspection door adjustment, so compare
its materials/shapes rather than claiming pixel-identical doorway lighting.
The window and bed comparisons have the same open-door state.

## Asset validation and provenance

All runtime archive checksums passed before integration. Eight GLBs preserve
original instance transforms, owner removal and local source envelopes. Native
environment tests exercise these bindings, material factors, opaque surfaces,
door movement/removal, existing destruction/studs, world immutability, fallback
and repeated GPU lifecycle. The v4 loader accepts the complete set atomically,
otherwise retaining v3. Individual missing-v4-file fallback was code-reviewed,
not separately fault-injected in this pass.

Runtime package: https://drive.google.com/file/d/1AtaOAhyZsI1mpOSBAUCzputbPtb_OgIc/view

Archive SHA-256: `e54814611da429fb3d451a5cba6d4f39c52c94138c659a8e66c5002de07c4d30`.
The GLBs live in `assets/environment/motel_room101_v4/`; supplied contracts,
licenses and QA are in this handoff's parent directory. Embedded texture maps
are retained in the GLBs; redundant loose maps and download copies are omitted.
`RUNTIME_SHA256SUMS` records the original archive layout, not this installed layout.

The artist verified the editable source backup by reconstructing nine downloaded
parts and checking 141 payload hashes. It remains on Drive without another local
source copy: https://drive.google.com/drive/folders/1cTHfhPDlX5XIMHiShJ37yJtzStd3TZd2

Source archive SHA-256: `280dd234657441926c01013fdfc90aff363ab14bc0e978b7becfbca4c687d6af`.

## Performance sample

Native Windows GTX 1060 3 GB, seed 42, 30 warmup and 300 measured frames:
`performance_tool.exe --motel --indoors --fire --hidden --frames 300`.
Both runs: 147 objects, 9 actors, 36 shots, 1 destroyed object, audio disabled.

| Milliseconds | Before: v3/downward lamps | After: v4/six-face lamps |
| --- | ---: | ---: |
| Total mean | 21.540 | 26.069 |
| Total median | 19.643 | 22.613 |
| Total p95 | 35.114 | 43.164 |
| Total maximum | 65.946 | 112.461 |
| Simulation mean | 3.358 | 3.612 |
| Draw submission mean | 18.049 | 18.394 |
| Present mean | 0.133 | 4.063 |

This sequential pair on a busy workstation includes both art and renderer
changes. The +4.529 ms mean is a measured cost, not an isolated GPU benchmark.
Character draw calls rose from 1735 to 2150; both prepared 1320 poses. Logs are
in the reused `build/swat/review/` directory. Further performance work should
target dynamic shadow submissions and presentation/GPU waits.

## Remaining art work

The bedframe and basin now have curved geometry, and the linen is thinner.
The room still looks sparse and uniformly clean. Material variation, furniture
detail and dressing remain useful next steps. The new luggage rack/wastebasket
handoff is ready for a separate prop integration; it is not part of this v4 set.
