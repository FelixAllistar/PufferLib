# Room 101 option 1: seated hinge hardware candidate

Candidate only. Native integration, runtime physics/collision, replication and final visual acceptance remain open. No real-world load or strength claims.

Clearance scope: the new hinge/slab/casing candidate passes its scoped tests. The full dressed scene is NOT globally interference-free: the unchanged optional source threshold overlay retains its documented +0.4 mm overlap with the slab throughout the checked swing. This inherited condition is outside the approved hinge edits and was not altered. Read the independent audit for exact test pairs and limits.

## Import and coordinate contract

All four runtime GLBs use the original exported source coordinate system: metres; X across the closed slab, Y up, Z depth. No bounds normalization. Leaf datum stays (0,0,0); width1.08m,height2.19m. Facade GLB retains its original datum; inspection assembly places facade at +1.74m X relative to leaf.

Use the supplied proper rotation (-source Z,source Y,source X), then original slab centering (0,-1.095,-.54). Do not center using hardware/handle bounds. Logical anchors remain (0,-.865,-.54),(0,.005,-.54),(0,.825,-.54). Other leaf sizes scale engineY by half-height/1.095 and engineZ by half-width/.54; world placement/yaw follows. Scaling may change physical bore/clearance; numerical evidence is for original1.08×2.19m only.

Clockwise source XZ inspection swing is x'=x*cos(theta)+z*sin(theta), z'=-x*sin(theta)+z*cos(theta), original axisX=Z=0. All movable leaf nodes must share that same transform. Mount nodes R101_H01_MOUNT/H02/H03 have source anchorsY=.23/1.10/1.92. Original damage probe barrel centres wereX=.018; new seated barrels useX=0. Keep probes and logical anchors explicit, not inferred from visual backplate or mesh bounds. The game edge-latch is separate and unchanged.

## Runtime files

- room101_leaf_option1.glb: accepted leaf minus three old cosmetic barrels, with only the approved three local reliefs.
- room101_facade_option1.glb: only offending inner jamb edge/bevel vertices retreat6mm. Outer casing boundary stays fixed. Other facade parts remain unchanged. Hinge-named optional casing/caulk remains trim.
- room101_hardware_option1.glb:9 intact mesh nodes plus3 named mount empties. Three independently addressable LEAF_METAL,FIXED_JAMB_METAL,PIN groups.
- room101_failure_remnants.glb:24 independently named fragments,8 per attachment. Load hidden initially. Standard glTF does NOT guarantee these nodes are hidden automatically: application owns visibility/state. Do not display the entire remnant file on intact supports.

Do not retain original decorative barrels as a second hinge. Old source merged-leaf components4,5,6 map respectively to H01,H02,H03. Exact original component vertex/polygon IDs and source bounds are recorded in the source inspection mapping. All unaffected original components remain.

## Ownership and failure table

| Node suffix | Parent | Intact | Corresponding hinge fails |
|---|---|---|---|
| LEAF_METAL | moving slab | visible | hide |
| FIXED_JAMB_METAL | fixed frame | visible | hide |
| PIN | fixed frame | visible | hide; optional engine debris requires an explicit transient copy, not a retained bridge |
| LEAF_EXPOSED_WOOD_REMNANT_0..2, LEAF_METAL_REMNANT | moving slab | hidden | show |
| FRAME_EXPOSED_WOOD_REMNANT_0..2, FRAME_METAL_REMNANT | fixed frame | hidden | show |
| MOUNT | logical reference | no geometry | retain identity; failed status belongs to engine |

One failed hinge leaves the other two intact. Last-hinge review pose keeps only H03 intact and yaws45degrees about the same logical axis. It is an art state, not a simulated load-bearing sag test. Detached example removes all intact hardware connections; leaf fragments travel with detached slab,frame fragments remain fixed. Falling,collision,surface clipping and live/late replication remain engine-owned.

## Geometry and finish

Provisional art dimensions: knuckleOD12mm,boreID6.6mm,pinOD6mm. Fixed top/bottom knuckles separated by1mm axial gaps from central leaf knuckle. Three slab reliefsR7mm,height100mm at approved heights. No extra mounting mortise. Screws and bolt holes are not modeled; seating evidence establishes geometric surface contact, not a fastening or strength calculation. Leaf wraparound surface plate contacts original+Z face; jamb plate sits outside retreated inner jamb surface in the unswept quadrant.

No newly authored materials or image maps. Intact hardware reuses existing satin metal. Exposed wood fragments reuse the existing project materials_v1 door_wood material: three unchanged512px maps, +V grain,0.6×2m physical tile, CC0 provenance already bundled with Room101. Facade and slab materials/maps remain unchanged. Fragments are visual attachments with uneven grain-aligned splinter silhouettes. They do not create broader wall/slab damage cuts; all damage beyond the three approved local reliefs remains engine-owned. This is not a damage simulation.

Read MANIFEST.json for node triangle/material counts. Read source/exact_face_change_audit.json for changed/removed/retessellated face attributes and source/casing_edge_vertex_changes.json for exact edge displacement.42 of44 original slab polygon signatures remain unchanged before GLB triangulation; two hinge-edge polygons are locally retessellated. Original decorative barrel polygons are deliberately removed. All remaining leaf component geometry/UV/material signatures unchanged. GLB re-export regenerates normals, on27 unchanged bevel triangles,with maximum normal-angle drift approximately0.0081degrees; see independent QA. Casing change affects48 imported seam-duplicated vertices of one component; exterior boundary remains fixed.

## Review and provenance

QA renders are made by importing actual exported GLBs into a new scene, never from an unexported modeling view. See qa/render_notes.json (compact package previews are JPEG copies of the rendered PNGs) and the independent numerical audit supplied with the handoff. Contact/separation limits and sampling resolution must be read from that audit; rendering alone is not clearance evidence.

Frozen input: Room101 v2 runtime review, Drive file1OsWdtrOmgvHDr49SPdDNF9k2jqGpOV1p. Original leaf SHA256 cf2cd3c1e521d53cd9e799b1035737f7247badf6d1d8b1cc32286934f7f89afc. Original licenses remain in source/LICENSES. Geometry additions are procedural project work; no third-party hardware assets introduced. Original frozen source GLBs are supplied separately for reproducibility and comparison, not replaced in place.
