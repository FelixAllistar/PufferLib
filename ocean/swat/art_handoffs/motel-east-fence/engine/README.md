# East fence — engine integration

Three original 2 m panels and one terminal post form an open-ended east run.
Panel roots are `(12.5,-.08,6/4/2)`, yaw +90 degrees; the terminal is at Z=0.
Ground top is Y=-.08. Source dimensions, materials and geometry are retained.
Runtime GLBs live in `assets/environment/motel_fence`; editable Blender source
and delivered placement/provenance are retained one directory above.

The native C importer (`tools/import_motel_utility.c --fence`, from `ocean/swat`)
produces `motel_fence_data.h`; default utility export remains byte-identical.
It welds coincident source positions and groups complete connected components,
never clipping original wire triangles. Each panel has 29 spatial groups of at
most 240 triangles. Three instances share those 29 collision meshes. Aggregate
owners 155–157 are inactive rendering proxies; terminal 158 remains independent.
The appended 87 pieces bring the canonical count to 1,044. Network/replay version
12 rejects older journals. Explicit 147/155/159-object legacy prefixes retain
the corresponding original mesh binding.

Bullet queries use actual diamond openings and nearest outward mesh triangles.
Across both faces, 1,976 test rays pass through openings and 424 hit wire; four
posts are solid. A target takes 34 damage through air, 26.273 through thin wire,
and zero through a post. Controller/capsule tests stop at panels and clear both
ends. Wire/rail acoustic groups are porous rather than solid bounding-box slabs.

Normal finite/interruptible charge placement now opens a local route. The test
places a charge, retreats, detonates, checks 18 removed/11 retained groups,
unchanged adjacent bays/posts and no officer injury, then walks the real
controller through. Late replica and reset agree. Wire/rail charge resistance
approximates thin fastening failure (2.5/3 mm effective thickness); it is not a
measured structural stress model. Posts remain strong; loose/bent wire and falling
rail debris are unfinished. Ordinary bullets do not erase wire groups.

The renderer uses the original model intact, or a cached surviving-triangle
material batch after damage. It does not submit one model per collision group.
Original UVs/normals/materials survive filtering. Missing art falls back to exact
surviving physics triangles instead of invisible walls or opaque bounds.

Native intact/cut/fallback proof is retained here. Full Windows graphics, motel
controller, replica and replay/save checks pass; Linux motel, acoustics, simulation,
UDP, tactical and encounter checks pass. Logs: `build/swat/review/fence-destruction`.
The fence mover warning is resolved for tested routes; unrelated dense room props
can still reach Box3D's 256-triangle mover query warning. Earlier detail/grazing
images retain the previous ground material; the cut proof uses the new asphalt.
