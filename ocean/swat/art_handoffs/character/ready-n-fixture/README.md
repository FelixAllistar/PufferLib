# Shared Ready N standing-empty character review export

Start with `standing_empty_shared_ready_n_original_timing.glb` and `bindings.json`.
Clip ID: `standing_empty_shared_ready_n_carry_f_original_6s`
Animation name: `Standing Empty / Shared Ready N Carry F`
GLB SHA256: `3a20375ec72f106925ef96718da0931e172f21908c6720cff4d792c173c0cc81`

This separate character export preserves the original 0–6 second study at 24 fps,
including subframe keys. It is not runtime activated or retimed. Historical
standing F and the committed rigid rifle remain unchanged. The new derivative's
letter F does not identify the historical F fixture.

## Contents and fidelity

70 native joints, 76 nodes, five skinned meshes/six primitives; one non-looping
action, 210 LINEAR glTF channels. Native source has 700 curves/416,500 LINEAR
keys. Both physical magazines stay visible throughout. There are no visibility
channels or animated reparenting. Rig root is identity, meters, +Y up; Blender
conversion is (x,y,z) -> (x,z,-y). Four neutral materials, no image dependencies.

The body preserves all 24,850 vertices and all seven influences, with two
JOINTS/WEIGHTS sets. UV/material seams produce 31,687 exported body vertices;
444 exported vertices need more than four influences (416 native vertices).
Weights are normalized equivalents of source sums. Every rendered bind/weight
vertex and oriented surface triangle is preserved. Six unused loose rifle
vertices are omitted; its 10,273 triangles are preserved.

All rendered vertices were compared at 2,204 times: 240 Hz, every native/export
key, exact listed source events and event +/-10 microseconds. Native evaluated
skin was also checked at 28 control times. Worst standard glTF difference:
0.000955083 mm; Blender reimport difference: 0.001308410 mm. Eight-influence
preflight passes and the four-influence profile rejects as expected. See
validation.json and animation_channels.json. Float32 export timestamps and
standard glTF shortest-path quaternion interpolation are measured numerical
equivalents, not a claim of identical arithmetic at every continuous time.

## Fit and current engine interface

`measured_preview_fit.json` supplies measured root, Ready basis, sole-support
vertices and a reversible, unbaked preview placement at unit scale. The source
sole floor is Y=0.003999979 m. Heading is explicitly defined from the Ready
rifle's horizontal direction, not guessed from the root. This preview placement
is not an accepted controller retarget.

Eye fit is unresolved: there are no eye bones or measured anatomical eye/visor
landmarks. Head-joint origins are inspection references, not camera-eye targets.
The helper Muzzle is approximately 0.37585 m from the physical muzzle; do not
use it for firing. A verified same-geometry bridge maps the rigid stock-pad
frame into Prop_Rifle. Use its measured muzzle/sights with the bridge if drawing
the separate rigid asset in an animation preview. Gameplay pose stays authoritative.

Current baseline is [5f71596d8](https://github.com/FelixAllistar/PufferLib/blob/5f71596d8bed78048d10b0a454bd3a6b02255327/ocean/swat/WEAPON_ART.md),
protocol/replay v8. The existing rigid loader requires two unskinned meshes and
uses mesh index 1 as its seated magazine. This character GLB is a separate
consumer input. Full-body playback, anatomical hand mapping and cosmetic prop
persistence are pending. Resolve duplicate rifle/magazine rendering and the
procedural reload hand path before integration. The old .56/.055 m geometry
baseline is historical; current measured stock-to-muzzle length is .9010567 m.

## Events, ownership and persistence

`VISUAL_EVENTS_AND_OWNERSHIP_READY_N.json` is copied byte-for-byte as authority.
`events.json` preserves all 26 decimal event values and rational representations
of those exact decimals; sampled contact grid coordinates are separately labeled.
The embedded source_H_pose_keys_seconds dictionary is historical, not runnable.
5.8 s means open approach/closure start; opposed support is sampled at
5.954166666666667 s, first geometric contact at 5.9625 s with its 240 Hz bracket.
Floor impact means the authored ballistic-to-settle transition at 4 mm clearance.

Old magazine releases at .88 s and finishes on the floor; fresh is picked up at
1.4 s and seated at 3.65 s. Actor/rifle/tool return to Ready, but magazine states
do not loop. The drop is baked under the GLB root: an independently world-fixed
cosmetic drop requires a captured actor-world transform and deduplication.
Only a fresh isolated preview resets initial props. Gameplay cancellation,
seeking, F5/F9 restore and return to idle must reconstruct persistent committed
magazine_seated/inventory/chamber state and cosmetic ownership. Animation never
grants ammo. Mapping to engine empty stages39/104/156 at60Hz remains pending;
a uniform six-to-2.6-second speedup is not approved or baked.

## Materials, source quality and reproduction

CHARACTER_MATERIAL_INVENTORY.json identifies nine surviving original 2048-square
character maps with hashes and exact body/UV compatibility. They are not embedded
here. These are non-PBR diffuse/specular/glossiness sources, not measured metallic/
roughness maps. Original graphs left glossiness unlinked. The extra sleeve is
outside that compatibility proof.

Known defects remain: native sleeve/link folds, acquisition finger crowding,
brisk departure, and intentionally off-shoulder working carry. Numerical export
parity does not certify artistic approval, continuous clearance, animated-normal
parity, full Khronos conformance or engine/GPU playback. native_authoring_comparison.mp4
is the existing native-source comparison; native_*.png and reimport_*.png are inspected matched static Ready/seat views
with identical camera, lighting and AgX exposure. These are not engine playback.

SOURCE_PROVENANCE.json pins the editable/action/archive/event inputs and earlier
H/N sources. Obtain the separate self-contained authoring checkpoint to regenerate
source. Blender4.3.2 exported without saving it. Tools accept the source GLB/work
paths as arguments; capture_parity.py expects the verbatim event file beside GLB.
Use export_fixture.py, capture_parity.py, then validate_fixture.py as documented
in their headers. Numerical validation needs NumPy/SciPy; runtime does not.
Check files against manifest.json. Keep all source art and this package private;
public redistribution rights have not been established.
