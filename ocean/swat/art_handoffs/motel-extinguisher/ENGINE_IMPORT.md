# Wall-mounted extinguisher integration

Original CC0 handoff: Drive file `1XLPh3U2TobK2nJ_QmLTbYih13Rskg-lA`.
All 34 archive payload hashes were checked before installation. The unchanged
runtime GLB, editable packed Blender scene, original atlases, source and numeric
QA are preserved. The font's original license is included. Python authoring
files are archival and were not executed. Drive retains the full backup;
redundant archive and preview copies are excluded from the repository.

Owner 1176 appends to the canonical motel without renumbering older objects.
The fixture sits beside room 104's door at `(4.20, 1.12, 0.224)` metres, with
identity scale/yaw and the original 275 × 546 × 224 mm proportions. Its two
measured bracket anchors resolve against facade 85's physical masonry polygons.
The source wall plane reaches the physical front face at Z = 0.09 m. Removal
of either required wall support removes the fixture's art and body together.
It remains a decorative fixture, with no pickup, discharge or pressure behavior.

All 2,412 original triangles supply rendering, collision and ballistic queries.
The bottle and rolled foot exceed the engine's 256-nearby-triangle contact
buffer. Thirteen meshes of at most 240 triangles retain every original face.
Dense curved parts use Box3D's `b3CreateMeshSubset`, which copies the original
edge flags and material indices across subset boundaries and recomputes the
serialized mesh hash. This preserves flat/concave edge behavior instead of
introducing cut seams. The metre geometry is built in centimetre units with
inverse shape scale to preserve small bevels. Full original geometry supplies
ballistic exit distances and the missing-art fallback.

The assembly currently uses a single steel physical material and finite strength
scaled from the authored 12 mm mounting spine (900 health). This is a game
approximation: the original bottle is a closed outer solid, rather than a thin
pressure shell, and the rubber hose, label and valve have distinct atlas
appearance without separate physical materials. No explosion or thin-shell
penetration behavior is implied. Original opaque 512px color/metal-rough maps
retain their source factors and texture ownership.

Version-17 maps retain the old 1,175-object junction prefix and 1,176-object
trolley prefix. Mounted/contact checks cover the actual wall contacts, support
and charge loss, damage, old maps, malformed recipes, late replicas and mesh
triangle/edge equivalence. Native graphics compare original/fallback silhouettes
against physical rays and show the actual facade placement. Full scenario
completion passes on Linux and Windows in 9,827 ordinary-input ticks, with
exact every-tick replay, mid-run save/resume, fresh replica and no contact-buffer
overflows. Contact tests preserve 28,395 triangles/edge flags across 24 assets
and 57,600 matched full/partition rays per canonical and replica check. Native
graphics match 154,496 stable ray/raster samples for the extinguisher across
five views and both source/fallback rendering. Texture ownership and final
release checks pass for both prop sources. Evidence is under
`build/swat/review/extinguisher`; this does not accept the complete game slice.
