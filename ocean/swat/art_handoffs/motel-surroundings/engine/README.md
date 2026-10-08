# Motel perimeter engine integration

The five original shoulder/bank placements use the delivered Y-up, metre-scale
geometry and transforms. `tools/import_motel_surroundings.cjs` checks the original
GLB hashes, closed oriented edges and render/collider triangle correspondence,
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
mesh data. Protocol/replay version 14 distinguishes this physical recipe.

The navigation grid retains 60 cm spacing and covers both ±30 m edges. Its
capacity remains within the 16-bit traversal queue. The node buffer is allocated
on demand by the existing navigation system.

`make -C ocean/swat surroundings-test` checks 6,970 support/seam rays, separate
rock thickness, ten live court/shoulder crossings, six repeated-module crossings,
real squad movement on both outer edges, bullet cover versus an uncovered
civilian, foliage acoustic transparency, replica reconstruction and teardown.
The native `test_environment_art.exe <directory> surroundings` adds matched
east/west/context captures, independent rock removal and missing-art fallback.

`source/surroundings.blend` retains the compact editable baseline with packed
maps. Original licenses and file hashes accompany the runtime and source.
The scanned-material candidate is reviewed separately; original procedural
materials, repeated foliage and rounded rock silhouettes remain provisional.
