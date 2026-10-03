# Animation asset contract and integration proposal

This document defines the renderer-facing boundary for the separate full-body
animation bank. The target branch is `5.0`; the verified remote baseline is
`794949ad98645a8b2499e8a90f09ae890dd7e04b` (rechecked 2026-10-03). Runtime
implementation details below are proposals unless explicitly marked verified.

## Verified published interface

`ocean/shenaniguns3d` currently contains a navigation simulator/controller and
a primitive-body viewer. It does not yet contain a skinned character loader,
reload inventory, or animation-state interface. Its README records that
`character.c/.h` came from the previously unversioned sibling `pd64`. The current
local game may have additional work; coordinate against that work before wiring
this contract into the renderer.

Sources pinned to the inspected commit:

- [README and qualification limits](https://github.com/FelixAllistar/PufferLib/blob/794949ad98645a8b2499e8a90f09ae890dd7e04b/ocean/shenaniguns3d/README.md)
- [Controller public API](https://github.com/FelixAllistar/PufferLib/blob/794949ad98645a8b2499e8a90f09ae890dd7e04b/ocean/shenaniguns3d/character.h)
- [Simulation conventions](https://github.com/FelixAllistar/PufferLib/blob/794949ad98645a8b2499e8a90f09ae890dd7e04b/ocean/shenaniguns3d/shenaniguns3d.h#L13-L44)
- [Render/camera code](https://github.com/FelixAllistar/PufferLib/blob/794949ad98645a8b2499e8a90f09ae890dd7e04b/ocean/shenaniguns3d/shenaniguns3d.h#L1394-L1494)
- [Build dependencies](https://github.com/FelixAllistar/PufferLib/blob/794949ad98645a8b2499e8a90f09ae890dd7e04b/ocean/shenaniguns3d/Makefile)

## Change boundary

The initial contribution adds this contract, a read-only GLB preflight, and
synthetic tests. It leaves controller, physics, trainer, input handling, state
machine and renderer behavior unchanged. No asset payload or authoring manifest
is part of the change.

A later isolated viewer/consumer adapter should be agreed with the engine owner.
Start gameplay integration with one idle and one directional walk before reloads
or prone transitions.

## Asset consumer requirements

Current full-body bank: 44 named clips, one skin with 74 joints, embedded textures,
maximum seven nonzero influences per vertex. Both character primitives have
`JOINTS_0/WEIGHTS_0` and `JOINTS_1/WEIGHTS_1`; preserve all sets. A four-influence
consumer must reject it clearly, not silently drop weights or renormalize a
subset. Any reduced-weight asset is a separate, explicitly reviewed derivative.

The published Makefile references Raylib 5.5. Its stock skinning data path has
four influences per vertex. Its GLB animation loader resamples at 17 ms and
stores one interpolation mode per bone. Our bank has 15 clip/node combinations
with mixed motion/visibility interpolation. Stock loading is therefore not a
verified consumer: use a separate tested sampler/skin adapter, preserving each
channel's interpolation and timestamp, before claiming parity. This is source
inspection, not a test of the local Raylib build.

Sources: [Raylib skin limit](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c#L5218),
[sampling and interpolation](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c#L5461-L5602).

Use exact clip names or an explicit stable-ID-to-name map; no index-only mapping.
Apply rest hierarchy and inverse binds, TRS channels, all weights, and normalized
quaternions correctly. The rifle is rigidly skinned to the right hand. Props use
joints and baked transforms; runtime Blender constraints are unnecessary.
`KHR_materials_specular` is used, not required; visual material parity remains a
separate gate if the consumer ignores that extension.

## Coordinate, movement, and camera boundary

Verified simulation: meters, +Y up, 60 Hz fixed tick; yaw zero faces +X. Forward
is `(cos(yaw), 0, sin(yaw))`; right is `(-sin(yaw), 0, cos(yaw))`. Render placement
uses `pd_char_feet_position`, not the rigidbody center. Standing/crouching nominal
heights are 72/40 Source units at 0.0254 m per unit. Actual stance is authoritative;
standing can be blocked even when requested. Simulator uses one Box3D substep;
README says original game uses four, so do not claim exact game parity.

GLB is Y-up at meter scale, with an identity scene-root transform. Authoring
records contain Blender-space Z-up floor values and matrices: do not copy those
raw values into the engine. Asset forward/yaw offset, soles/feet origin, eye
height, weapon sockets and model-to-capsule alignment must be measured in the
consumer before freezing a transform. No guessed 90-degree correction is part
of this contract.

Proposed ownership: physics/controller alone move the world root, resolve
collision, choose actual stance, and report grounded/velocity state. Render
animation reads that snapshot. Keep visual hip/limb motion; do not double-apply
root displacement. Crawl clips are explicitly in-place. Confirm root policy per
clip, and derive locomotion phase/rate from measured travel/cycle speed later;
the current bank is not controller-speed-matched.

The published viewer has orbit, first-person and chase cameras. This full-body
bank and the separate FPS/viewmodel bank are distinct. Do not use camera changes
to conceal full-body fitting errors or automatically reuse a viewmodel as TPS.
Decide body visibility/head clipping and camera/body/weapon ownership with the
engine owner. Body aim/fire and final ADS/gear fit are outside this bank's gate.

## Timing and prop ownership

Playback and events use seconds from clip start, not source frame numbers.
Authoring rates include 24 and 30 FPS; some tracks were exported at 60 Hz.
Read sampler timestamps. Exact authored reload durations: upright tactical
6.375 s, upright empty 5.625 s, prone tactical 8.15 s, prone empty 7.4 s. Float32
GLB endpoints can differ by sub-microseconds. Video endpoint-inclusive frame
counts are not animation durations. Event crossing tests should use a consistent
half-open interval, e.g. `previous < event <= current`, and explicit restart
initialization. Seeking/replay must not duplicate gameplay events.

Magazine visibility is binary uniform scale on `prop:Magazine*` joints with STEP
interpolation. Never interpolate or blend ownership scale. At a transfer exactly
one visible instance represents each physical magazine; motion can stay LINEAR.
Tactical precondition: old installed, fresh outer spare, inner slot empty.
Postcondition: old in inner slot, fresh installed, outer empty. Empty reload
leaves old dropped, fresh installed, both slots empty. These are authored preview
states, not a persistent inventory implementation.

Reference transfer times (seconds):

- Upright tactical: old grip 0.92, old stored 2.67, fresh grip 3.25,
  fresh seated 5.0916666667
- Upright empty: old drop 0.45, fresh grip 1.9416666667,
  fresh seated 3.7833333333
- Prone tactical: old grip 1.5, old stored 3.6, fresh grip 4.25,
  fresh seated 6.4
- Prone empty: old drop 1.0, fresh grip 2.4, fresh seated 4.55

The empty-mag track is anchored to the **asset root**, independent of hands and
hips. It will still move if the controller moves the entire asset. Proposed
runtime release: capture world transform at release, transfer to a gameplay-owned
world object, hide the corresponding character instance, and let gameplay decide
physics, persistence and cleanup. Never spawn a new magazine each render frame.
Do not mistake a baked preview fall for world collision or persistent inventory.

Gameplay owns ammo/chamber state, inventory IDs, reload accept/commit/cancel,
authority/network rollback and shot permission. Animation can publish visual
markers; it must not grant ammo just because a pose was sampled. Receiver/bolt
meshes are static in the bank: receiver gestures do not prove functional hardware.
An ordinary ready clip's default magazine visibility must not refill carrier
slots after a reload. Repeated reloads, interruption and blending need explicit
gameplay state plus tests before release.

## Public-asset and provenance boundary

No raw Mixamo character/animation, weapon-kit meshes, textures, Blender files, or
exported GLBs in this public PR. Keep them in user-controlled private storage and
load from an ignored local path. Code, synthetic fixtures, clip metadata and
hashes can be reviewed separately. Each asset needs source/product identification,
license evidence, authorized use/distribution decision, and exact content hash.
The repository's code license does not establish asset redistribution rights.
Do not resolve uncertain rights by automatically making this material public.

## Acceptance gates and open questions

1. Target branch is `5.0`. Confirm the current engine entry point and active files
   before runtime edits; the published snapshot may be only the training copy
2. Confirm consumer loader/skinning path, seven/eight influence capability, and
   per-channel interpolation support
3. Agree root origin/forward transform, actual stance timing, FPS/TPS bank choice,
   event authority, stable prop IDs and interruption policy
4. Structural preflight + synthetic negative tests; then a private real-asset
   consumer test at keys and between keys, especially transfer boundaries
5. Visual playback at real speed; yaw/translation, crouch clearance, fixed-tick
   vs render-rate independence; velocity/phase alignment; no four-weight damage
6. Before reload integration: two consecutive tactical/empty cycles, mid-transfer
   cancellation, zero/one spare, moving-root drops, restart, seek and rollback

Current preflight does not certify all glTF validity, deformation quality,
root motion, materials, event authority, animation blends, or gameplay. Published
simulator build tests were inspected but not rerun here: dependencies and a full
checkout were not provisioned. No gameplay files were changed.

## Preflight usage

From the repository root, using Python's standard library only:

```sh
python -m unittest discover -s tests -p 'test_animation_glb_contract.py' -v
python scripts/check_animation_glb.py /path/to/local/character.glb --expected-animations 44
python scripts/check_animation_glb.py /path/to/local/character.glb --max-influences 4
```

The last command is expected to reject the seven-influence reference bank.
The probe reads its input without modifying or uploading it. It checks dense
embedded-buffer accessor bounds for skin attributes, animation inputs and
magazine-scale outputs, weight sets and sums, nonzero influence count, animation
names/timestamps, and magazine STEP/binary visibility. It reports mixed
interpolation modes by target node index, with names for display. Other animation
output accessors and transforms are not validated by this limited probe.

Exit 0 means those limited checks pass; 1 means a contract mismatch; 2 means
malformed/unsupported input or an I/O error. Supply the actual consumer influence
limit explicitly. Passing does not establish support in any particular engine.
Sparse accessors and external buffers are outside the probe's supported profile.
Run a full glTF validator separately. The tests construct synthetic bytes and
need no Blender, game dependencies, asset license, or downloaded fixture.
