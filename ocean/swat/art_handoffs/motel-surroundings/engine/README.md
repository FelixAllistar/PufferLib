# Motel perimeter engine integration

The five original shoulder/bank placements use the delivered Y-up, metre-scale
geometry and transforms. `tools/import_motel_surroundings.cjs` checks the original
current GLB hashes, unchanged original collision hashes, closed oriented edges and render/collider triangle correspondence,
then generates `motel_surroundings_data.h`. No authoring scripts run on import.

| Placement | Shoulder soil | Bank soil | Separate stone owners |
| --- | --- | --- | --- |
| east_0 | 1044 | 1045 | 1046–1058 |
| east_1 | 1059 | 1060 | 1061–1073 |
| east_2 | 1074 | 1075 | 1076–1088 |
| west_0 | 1089 | 1090 | 1091–1103 |
| west_1 | 1104 | 1105 | 1106–1118 |

The world has 1,119 objects. Owner 0 and the earlier motel/fence owner IDs remain
unchanged. Fifteen shared closed triangle meshes supply 75 physical components;
none exceed 240 triangles. Ground uses SOIL and rocks use STONE. Foliage has no
collision, bullet resistance or acoustic occlusion. These terrain components
are static support/cover; terrain craters and loose rock physics are unfinished.

Intact rendering uses the original 62,240 triangles and 25 material batches.
Each bank's four materials share one cached visibility mask for its soil/foliage
and 13 rocks. Removed owners cannot leave their art behind. If art is absent,
the exact closed collision surfaces render as the fallback. Map reconstruction
validates the recipe before binding meshes and reset/close releases the shared
mesh data. Protocol/replay version 14 introduced this physical recipe; current
version 15 also includes bounded motel furniture contacts and navigation fixes.

The navigation grid retains 60 cm spacing and covers both ±30 m edges. Its
capacity remains within the 16-bit traversal queue. The node buffer is allocated
on demand by the existing navigation system.

`make -C ocean/swat surroundings-test` checks 6,970 support/seam rays, separate
rock thickness, ten live court/shoulder crossings, six repeated-module crossings,
real squad movement on both outer edges, bullet cover versus an uncovered
civilian, foliage acoustic transparency, replica reconstruction and teardown.
The native `test_environment_art.exe <directory> surroundings` adds matched
east/west/context captures, independent rock removal and missing-art fallback.

`source/surroundings.blend` retains the current editable scene with packed maps.
The scanned-material revision replaces the gravel, bank soil and stone surfaces
with Poly Haven Gravel Ground 01, Dirt and Rock Boulder Dry (CC0). Provider URLs,
hashes and measured scales are in `source/SCAN_PROVENANCE.json`; the runtime
license packet keeps the original-art dedication and scan rights distinct.
The 1K maps use runtime repeats of 8/3 m, 2 m and 1.8 m, with normal strength .65.
Geometry, collision, transforms, foliage and owner IDs are unchanged. Regenerating
the collision header produces byte-identical output. The three PNGs show the
native scanned-material revision, with missing-art and individual-removal checks
also passing. Original manifests remain under `V1_` names; current manifests
are the untouched delivered V2 records. Full source backups remain on Drive.

This improves surface detail, not the repeated foliage, rounded rock silhouettes
or incomplete wider setting. Those remain provisional.

2026-10-09 appearance update: v3 uses gravel factor 0.65, stone factor 0.78 and
lower asymmetric shrub forms within their existing envelopes. All original scan
images and closed collision are preserved. The importer still matches all 1,040
rock triangles and regenerates the same collision header. Packed editable source
and current runtime/source receipts are updated. The former 1,119-object prefix
now sits within the expanded 1,173-object ground layout. Native context and
current checks are in ../../motel-road-context/engine/VALIDATION.md.
