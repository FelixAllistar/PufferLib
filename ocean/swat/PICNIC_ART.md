# Motel picnic table

The original BC_PT01 table stands in the front court at `(6, -0.08, 7.5)` metres.
Its GLB is already Y-up and metre-scaled. The ground-centred pivot needs no
additional axis conversion or scale. The table surface is 0.760 m above its
feet; benches are 0.455 m high. This provides physical cover and environmental art;
there is no sitting interaction or animated seating rig.

`assets/environment/motel_picnic/BC_PT01_picnic_table.glb` retains all 5,752
source triangles, original UVs, two opaque materials and three embedded maps.
SHA-256: `832407ad8d7883de2bf045c7d3249ec2f6ee8dde72aeb92b04ef300f429fe8b4`.
The handoff's provenance is in `art_handoffs/picnic-r01/ENGINE_INTAKE.json`.
The original art was authored for this task; no blanket CC0 claim is made.

The four GLB mesh nodes contain 80 closed components. Engine ownership groups
them into 20 wooden members and 30 washer/head or washer/nut pairs. These 50
game owners are separate from the four source nodes. Every original triangle
belongs to exactly one owner. Removing a plank sheds its two complete fastener
pairs; neighbouring planks retain their original geometry and collision.

Four measured foot faces meet the existing foundation. The support graph uses
93 contacts verified against actual source triangles, with 63 cooked parent
anchor ray checks. A leg needs an attached bearer to retain its angled source
pose. Removing its last bearer sheds that loose leg; floor contact alone does
not hold it upright. These are bounded static fixture rules. Failed owners are
removed using the existing damage path; there is no dynamic joint, falling-table
or measured timber/fastener strength model.

Collision keeps the actual sparse source geometry, including drainage and
under-frame openings. It uses 56 contact subsets of at most 240 triangles, with
the full original mesh retained for penetration queries. Cooking in centimetre
units preserves small bevels; shape scale restores metre dimensions.

The intact visible table submits the original eight material primitives once.
After damage, up to eight cached source-triangle selections retain the same
material and texture owners. Rebuilds occur on owner-mask changes. Shadow and
contact passes select each submitted owner's original components independently,
so culling the first visible owner cannot erase another owner's shadow.
Selection meshes own geometry only and close before their parent model.
Missing art uses the owner's actual cooked triangles rather than an enclosing
table-sized box. These resource counts are not frame-time measurements.

Rebuild collision and render-selection data from the unchanged GLB:

```sh
node ocean/swat/tools/import_motel_picnic.cjs
```

The adjacent `motel_picnic_import.json` pins the source hash, component face
ranges, names and intended contacts. The importer verifies all source faces
are retained exactly once, checks contacts against GLB triangles, and writes
the generated header atomically. Changed source bytes and output aliases of
the protected GLB reject before writing. Original Blender/Python recipes were
not executed for engine integration.

`make -C ocean/swat motel-picnic-test` exercises ordinary movement, navigation,
live aim/fire, board/support failure, reset, encoded fresh replicas and old
protocol rejection. `motel-picnic-art-test-build` builds the explicit display
check; pass an environment-asset directory and an existing capture directory.
It compares the actual renderer with independent indexed source selections,
checks separately culled shadow owners, and verifies borrowed resource cleanup.
Windows and CMake expose corresponding test targets. Broad mission/replay
evidence and remaining native visual limits are recorded in the integration
receipt; a passing table increment does not complete the vertical slice.
