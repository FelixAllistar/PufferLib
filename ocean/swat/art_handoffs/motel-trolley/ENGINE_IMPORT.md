# Service trolley integration

Original CC0 handoff: Drive file `11pPGZtwgtEPJEGJs2SfNn2znSp7157fD`.
All 31 archive payload hashes were checked before import. The unchanged runtime
GLB is `assets/environment/motel_props/motel_service_trolley.glb`; editable
Blender, atlas images, authoring files and compact numeric QA remain here.
Python authoring files are archival and were not executed locally. Redundant
previews and archive copies are excluded from the repository.

The single two-shelf trolley is parked in the rear utility room at
`(-8.75, 0.0055, -9.35)` metres, with identity scale/yaw. Its original dimensions
are 899 × 924 × 588 mm. Owner 1175 appends to the existing canonical map, without
renumbering older owners. Four original wheel contacts resolve against floor
127's original triangles, not its bounding box: raised perimeter trim sits
above the actual tiles. This is a static parked object, with no rolling or
inventory behavior.

All 2,416 original triangles supply rendering, collision and ballistic queries.
Whole connected components are grouped into contact meshes of at most 240
triangles. Centimetre mesh construction followed by inverse shape scale
preserves small bevels; world geometry remains in metres. The original full mesh
supplies exit-thickness queries. Empty shelf bays remain empty. Missing art
renders the same surviving query triangles rather than a solid bounds proxy.

The assembly currently uses steel response and finite strength scaled by the
authored 8 mm caster fork (600 health). This is a game approximation. Rubber,
paint and liners differ visually in the atlas but do not yet get separate
physical materials. Trays retain their original 18 mm solid pans, and frame
tubes retain the original closed cylinders; this is not a thin-sheet or hollow
tube simulation. The two original 512px color/metal-rough maps retain their
authored material factors and shared texture ownership.

Ordinary rifle impacts below the scaled fracture threshold do not erase the
assembly. Sufficient damage removes the physical body and art together. Floor
127 is an immutable canonical floor, so floor-support loss is a generic fixture
capability rather than a newly enabled destructible-floor feature. Version-17
maps/snapshots serialize the same existing dependency/thickness fields. The old
1,175-object prefix still reconstructs the junction fixtures without the cart.

Validation uses `make -C ocean/swat mounted-props-test motel-contacts-test` and
native `test_environment_art.exe OUTPUT_DIRECTORY motel-props`. Checks cover
four floor contacts, open bays, separate liner exit thickness, finite strength,
removal/late join, old map prefix, malformed recipes, unsupported snapshots,
full/partition triangle and edge equivalence, and raster/collision agreement
with both source art and fallback. Evidence is under `build/swat/review/trolley`.
