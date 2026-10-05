# Environment art and authored locations

This adapter applies the small, CC0 house-kit runtime pack and a provisional
generated painted-plaster comparison in
`assets/environment/` to the **existing authoritative generated geometry**.
It is enabled by default in the player. It does not import the example house,
change layout tokens, or replace the neural/uniform generator or preference data.

## What is connected

| Live object | Visual recipe | Damage owner |
| --- | --- | --- |
| Drywall or plaster, including every independent skin cell | Calm painted-plaster base color by default; preserved weathered source selectable | That one `SwatObject` |
| Wood frames, supports, floors, roofs and coarse furniture | Worn timber base-colour texture on its exact box | That one `SwatObject` |
| Wood door | Imported `door_leaf.glb`, including panels, chips, hinges and knobs | The moving door object, all primitives together |
| Other materials, including glass | Existing graybox treatment | Existing object |

There is no cross-object damage mask or cached index table. The render recipe
is derived from the current object's material/door flags on each draw. Each
inactive object is skipped. Removing one board leaves the other face and studs
visible and physical according to their own authority state; removing a door
hides all its art. Health still darkens the surviving part. The bindings work
on the host and replicas without new wire fields, random choices, object-count
changes, or persistent IDs. Collision, penetration, acoustics, AI line of sight,
observations, rewards and layout acceptance remain simulation-owned.

Texture coordinates use metre-scale repetition with adjacent skin UVs aligned
in the wall's basis. Wood floor art is drawn on the actual floor box; its
old room-sized tint overlay is omitted, avoiding a second coplanar surface.
All finish floors use their existing material boxes without duplicate overlays. Planning still cuts
away the actual roof object. All main, planning, sniper and device views use
the same binding path. Debug mode adds box wires over the art.

The initial lighting pass covers both primitives and imported models using
linear diffuse shading, exposure, warm room lights and filtered sun/nearest-room
depth maps. Geometry changes invalidate immediately; moving silhouettes refresh
every four ticks. It remains an authored approximation without GI or scene reflection probes.
Metric normals/roughness, original model PBR factors, daylight reflections and
textured character shadows are now connected; see ENVIRONMENT_MATERIALS.md.
The generated-house shadow caster geometry follows authoritative boxes; decorative tabletop detail does
not cast its own shadow in this pass. See the player README for comparison and
exposure controls. The generated plaster image has its own prompt/hash record
in `painted_plaster_v1.json`; it makes no CC0-source or measured-relief claim.

## Generated tabletop props (recipe v1)

Six existing Household Clutter Batch 02 models dress **accepted generated
furniture**: chipped mug, stacked plates, folded towel, leather wallet, TV remote,
and eyeglasses. They form three small domestic pairs. The generator currently
identifies halls, not kitchen/bedroom/bathroom roles, so these are neutral tabletop
clusters rather than invented room semantics. Cedar House and the annex are unchanged.

`environment_props.h` is the pure presentation metadata/hook. It uses accepted
layout seed, token fingerprint and furniture ordinal to select a pair and its
orientation with fixed integer arithmetic. It does not draw from the simulation
RNG, run the neural policy again, change tokens/scores/preferences, or add wire
fields. Replicas reconstruct the same accepted plan from the existing map packet.

Each furnishing is matched to a unique current active, solid object by its
accepted position, size and material. Missing, ambiguous, door, tilted, resized,
or destroyed supports fail closed. The hook is rebound every draw, so no object
index survives a map reset. Each prop inherits support health tint and disappears
with its support; there is no invented falling/debris simulation. All camera
paths use the same pass before actors and transparent windows.

These props are explicitly decorative: no independent collider, bullet stop,
interaction, sound blocker, AI occlusion or damage state. The supporting wooden
box remains fully rendered and authoritative. Prop height is below 10 cm and the
entire XZ footprint is at least 8 cm inside that existing solid footprint. No
walking or doorway space is occupied, and no solid furniture silhouette is
replaced by a hollow art mesh. Tiny visual occlusion from tabletop detail is not
represented in policy observations; these assets are unsuitable as tactical cover.
Real furniture archetypes require a separate gameplay/collision/damage contract.

The six GLBs add **836,672 bytes** (about 817 KiB), 12,707 distinct triangles,
25 material primitives and 19 embedded 128px base-colour maps. Geometry is copied
bit-for-bit from the source pack. `props_catalog.json`, `build_props.py`, and
`validate_props.py` record and validate provenance, bounds, hashes and conversion.
Their CC0 source license is copied in `LICENSE_SOURCE_CLUTTER.txt`.

At most 24 instances are drawn per generated view (two per existing furnishing).
The hook allocates no heap memory and scans at most 12 × 1,536 support candidates;
models are loaded once per view, not per instance. There is no extra headless or
network payload. This is a bounded first pass, not a whole-library performance
claim: distant LOD/culling and instancing remain future work. Missing/corrupt or
out-of-bounds models are omitted individually, leaving the real support visible.
The existing art opt-out and explicit asset-folder override apply to props too.

## Door transform contract

The packed GLB has one identity-transform node, no authored open angle, no
static casing and no imported collider. Its **slab**, not its detailed-model
bounding box, is normalized to `[-0.5,+0.5]` on every axis:

- X = thickness, Y = height, Z = width
- origin = slab centre; hinge edge = local Z = -0.5
- scale = twice the live object's `half` extents
- then the existing object pitch, yaw and centre translation are applied

This preserves the game's closed/open/intermediate poses and its 80 mm
house-door floor gap. Knobs and hinges protrude visually as ordinary detail;
they have no extra colliders. Do not use `GetModelBoundingBox` to normalize the
door: knob protrusions intentionally exceed the logical slab bounds. Annex
doors use their larger authoritative dimensions. Their details scale with the
leaf; this first slice does not provide separate art for every door family.

## Assets, provenance and size

`assets/environment/catalog.json` is the versioned import/audit record, with
source/output hashes, exact transformations, logical bounds, and ownership.
Runtime filenames are fixed by `environment_art.c`; the game does not parse
the JSON or original Blender manifests. The included builder/validator can
reproduce the small pack from the unmodified house-kit sources. The source
pack and editable Blender files are intentionally not copied into Git.

The original three runtime art files total **290,507 bytes** (about 284 KiB): two 256 ×
256 base-colour textures and a 1,236-triangle door. See the catalog for exact
per-file sizes and hashes. With the props, the CC0 source slice totals **1,127,179 bytes**
(about 1.08 MiB). The separate generated plaster comparison adds 3,069,691 bytes;
the combined runtime images/models total 4,196,870 bytes (about 4 MiB).
Catalogs/licenses/build tools are additional text files. Source licensing is
recorded in the `LICENSE_SOURCE_*.txt` files.
The source surface photographs are Poly Haven CC0; the generated plaster
derivative and house-kit geometry have the supplied CC0 dedication.

Raylib 5.5 loads the embedded glTF base-colour materials. The initial linear
lighting shader applies to them as well as immediate geometry; the unlit fallback
retains simple face tinting. Neither path claims Blender/PBR lighting parity.
Textures/models are loaded once per view and released before the graphics
context closes. Headless simulation does not initialize or load this cache.
No texture/model content is uploaded by the player.

## Builds and fallback

The standalone Makefile, portable CMake player and Windows build script copy
the runtime assets and licenses beside the binary. The launcher watches the
asset folder for changes. Search order is the executable's `assets/environment/`
then the repository's `ocean/swat/assets/environment/`.

- `SWAT_ENVIRONMENT_ART=0` selects the previous graybox presentation.
- `SWAT_ENVIRONMENT_ASSETS=/path/to/pack` chooses one explicit asset folder.
  An explicit folder never silently mixes in files from the default pack.
- Missing assets produce a warning and fall back per piece. A missing door
  model uses the textured box if wood is available, otherwise its graybox.

The root training `build.sh` includes this adapter in its SWAT source list.
Its simulation source-list fix is already present in the base of this prop slice.
Standalone Makefile/CMake checks do not qualify the full trainer build.

## Verification

From the repository root, with the README's pinned Box3D and Raylib dependencies:

```sh
make -C ocean/swat test net-test
make -C ocean/swat sanitize
python3 ocean/swat/assets/environment/validate_props.py --header ocean/swat/environment_props.h
make -C ocean/swat viewer environment-art-test-build
./build/swat/test_environment_art build/swat
./build/swat/swat --mission generated --layout-seed 42 \
  --capture build/swat/generated-art.png --capture-screen plan
SWAT_ENVIRONMENT_ART=0 ./build/swat/swat --mission generated --layout-seed 42 \
  --capture build/swat/generated-graybox.png --capture-screen plan
```

The explicit graphics check requires a display/OpenGL context and is not
registered in headless CTest. The headless binding test is included in both
`make test` and CTest. It verifies 32 generated maps, independent skin removal,
replica selection, each cardinal door yaw and every swing step against actual
collider transforms, plus the fixed hinge and floor gap. The graphics test
checks real imported pixels, inactive visibility, immutable world state,
generated rendering, missing-assets fallback, opt-out and repeated lifecycle.
The prop test covers 96 uniform/neural layouts across all difficulties, real
map/snapshot encode/decode replication, exact repeated recipes, support damage,
ambiguous/tilted support rejection, all six prop kinds and footprint clearance.
Graphics checks render each imported prop, capture three real generated tabletop
clusters and verify per-asset omission, support visibility and repeated cleanup.

## Next integration slices

The catalog is an initial import boundary, not a claim that all environment
batches are playable. Furniture meshes, exterior set pieces, texture families,
LOD selection and broad procedural dressing are not yet connected. A later
slice can attach explicit visual archetypes while keeping accepted placement,
clearance, collision and independent damage ownership authoritative. Full
assembled wall GLBs must not be overlaid on existing damage cells: that would
cover real holes with surviving art. New visual variants that change perception
or collision need intentional, versioned design and multiplayer qualification.

## Briar Court motel

`./swat play --mission motel` selects the complete motor-court encounter. The
Houses planning tab also offers Briar Court. The CC0 batch-15 import adds 24
new modules, 16 reused designs and 146 placed instances. The source GLBs remain
unchanged in `assets/environment/motel_v1`; source licences, manifests and
hashes accompany them. The preserved handoff README describes the complete
Blender source package; this runtime directory contains the modules and manifests,
with the complete editable package retained in the art handoff.
`tools/import_motel.py` generates the deterministic
local mesh data and placement table from hash-checked inputs, converting
source Z-up to engine Y-up once. It never executes supplied Blender scripts.

Each static instance has its own authoritative object and exact triangle
collision, retaining facade openings, bathroom passages, the access ramp and
furniture gaps. Five separate door leaves use the existing physical hinge,
interaction, lock and breach machinery. A removed leaf hides its whole matching
model. Static masonry and furnishings currently remain intact. Room volumes,
occupants, staging and extraction form a playable encounter. Planning omits the
roof collection; shadow, officer, scope and device rendering share the instances.

The standard map packet still transmits object identities and bounding recipes.
For this new mission, matching compiled placements reconstruct triangle collision
on replicas; all instance positions, sizes, yaw and material/door flags are
checked before any collider changes. Modified maps that no longer match retain
the explicit box geometry they transmit. Existing missions and packet fields
are unchanged. Both peers need a build that supports the new motel mission.
The world owns collision meshes and releases them after its physics world.

The adapter restores original scalar roughness and metallic factors omitted by
Raylib 5.5 when no combined texture is present. Materials use derivative normals,
original base colour, mipmaps and the common lighting shader. Lighting includes
an analytic daylight environment and room reflection approximation, not captured
scene reflections. The visible sky uses the same daylight radiance. The next
art pass is adding metric normals/roughness to the motel's small source textures.
Master module reuse currently retains the room-101 plaque texture/geometry and
master facade finish, rather than the assembled example's per-room variants.

Engine validation covers all nine authored 0.6 m diameter / 1.8 m capsule
routes against Box3D mesh collision, closed/open/breached doors, every imported
material factor, map round-trip collision (including already-open doors),
modified-map fallback and repeated world reset/shutdown. Linux GPU captures
and the standard lighting, rifle and environment graphics checks are also
reviewed; rendering remains read-only with respect to gameplay state.

## Morrow Block storefronts

`./swat play --mission storefront` or Morrow Block in the planning menu selects
batch 16's laundromat, pawn/repair shop and shared rear service area. The 24 new
modules and 15 reused designs retain their exact source GLBs and embedded
metric base colour/OpenGL +Y normal/roughness maps in `storefront_v1`. The 151
modular placements plus a separate site asset use original static triangles.
The site includes all eight source sidewalk/apron/street meshes, extracted with
only their referenced geometry/material buffers; unrelated example nodes and
textures are excluded. `tools/import_storefront.py` verifies module hashes and
records provenance for the extracted site. No supplied Blender scripts execute.

Five leaves use their measured original floor hinges with functional closed,
open, locked/picked and breached states. Example open angles become closed
canonical rest angles; the rear exit's opposed orientation opens inward.
Existing doors retain their original behavior. Three room volumes, two gunmen,
three civilians, staging/extraction and squad spawning form the encounter.
Physics uses canonical door slabs and exact static triangles for openings,
thresholds, furniture and the authored site. Static imported masonry, windows
and furnishings currently remain intact, with one gameplay material per module;
this is not per-primitive ballistic material assignment or destructible glazing.

Map replicas reconstruct the compiled static geometry only after every instance
recipe matches, including hinge/current transforms for already-open leaves.
Modified maps retain transmitted box collision. Existing enum indices, map
fields and protocol/replay v8 remain unchanged; both peers need storefront support.
Mesh data is owned by the world and freed after its physics world is destroyed.

Only the current mission's location modules load onto the GPU, before camera
and shadow passes. Switching missions unloads the previous bank; ordinary houses
load neither large kit. Scalar PBR factors are restored alongside embedded maps.
Opaque primitives draw first. Blended glass draws after scene geometry, ordered
back to front for each camera with depth writes disabled, then restores render
state. Glass no longer hides later-drawn interiors or casts opaque pane shadows.
This sorting is per placed object; intersecting translucent primitives within
one module are not independently sorted. There is no scene refraction.

Validation covers all five authored capsule paths, actual controller walking
across the front/stockroom thresholds, five functional leaves, original static
mesh identity, resting/open map reconstruction, modified-map fallback and reset
ownership. GPU checks validate all 40 motel and 40 storefront models/materials,
only eight meshes in the extracted site, on-demand switching and idempotence,
and visible objects behind glass with correct depth state. Windows and Linux
player captures use the same asset placement and shared lighting.
