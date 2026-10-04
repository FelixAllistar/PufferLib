# Procedural environment art

This adapter applies the small, CC0 house-kit runtime pack in
`assets/environment/` to the **existing authoritative generated geometry**.
It is enabled by default in the player. It does not import the example house,
change layout tokens, or replace the neural/uniform generator or preference data.

## What is connected

| Live object | Visual recipe | Damage owner |
| --- | --- | --- |
| Drywall or plaster, including every independent skin cell | Worn plaster base-colour texture on its exact six collider faces | That one `SwatObject` |
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
Other room floor overlays keep their existing behaviour. Planning still cuts
away the actual roof object. All main, planning, sniper and device views use
the same binding path. Debug mode adds box wires over the art.

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
per-file sizes and hashes. With the props, runtime files total **1,127,179 bytes**
(about 1.08 MiB); catalogs/licenses/build tools are additional text files. Licensing is recorded in the two `LICENSE_SOURCE_*.txt` files.
The source surface photographs are Poly Haven CC0; the generated plaster
derivative and house-kit geometry have the supplied CC0 dedication.

Raylib 5.5 loads the embedded glTF base-colour materials. This path uses
base-colour rendering and simple face tinting, not Blender/PBR lighting parity.
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
