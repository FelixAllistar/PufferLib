# East fence — engine integration

Three original 2 m panels and one terminal post form an open-ended east run.
Panel roots are `(12.5,-.08,6/4/2)`, yaw +90 degrees; the terminal is at Z=0.
The ground top is Y=-.08. Source dimensions, materials and geometry are retained.
Runtime GLBs live in `assets/environment/motel_fence`; the editable Blender
source and delivered placement/provenance are retained one directory above.
The assembled preview GLB is deliberately not another runtime copy.

The native C importer (`tools/import_motel_utility.c --fence`, run from
`ocean/swat`) produces `motel_fence_data.h`. Its default utility export remains
byte-identical. Three panel instances share one mesh; the terminal has its own.
There are 12,796 rendered/collision triangles and four added owners, 155–158.
Canonical map object count is now 957. Network/replay version 11 rejects older
recordings with incompatible object IDs. Original 147/155-object legacy map
recipes still reconstruct their corresponding mesh prefix.

Bullet queries use the actual diamond openings. Penetration finds the nearest
outward triangle through the mesh BVH instead of treating the aggregate bounds
as solid steel. Tests sample both faces: 1,976 rays pass through openings and 424
hit thin wire; four posts are solid. A test target takes 34 damage through air,
26.272 through wire and zero through a post. Standing capsule casts block at
all three panels and clear both ends; the real character controller also stops
at the fence and physically walks both ends. Network replicas repeat the queries.

This is a static fixed assembly, not completed fence destruction. Local charge
cutting, loose wire and severed posts remain work for the vertical slice. The
detailed mesh can reach Box3D's 256-triangle mover query warning; the tested
routes stop correctly, but collision decomposition remains necessary before
claiming complete coverage of every approach/stance. No invisible solid sheet
was added to bullet queries or navigation.

Native context/detail/grazing proof is retained here. Full validation logs are
in `build/swat/review/east-fence`. The asphalt candidate is a separate pending
material change; these fence screenshots retain the earlier court material.
