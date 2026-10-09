# Physical motel junction-box integration

Original CC0 handoff: https://drive.google.com/file/d/1wuzLVBq-LDzlvtffXkkDWPldOTuZwz0X/view

The original 108 × 580 × 58 mm GLB, editable source and texture originals are
retained here and under `assets/environment/motel_mounted`. The complete Drive
archive was verified against all 25 payload hashes. `manifest.json` and
`SOURCE_README.md` describe the original source-art handoff; this document records
the engine integration. Authoring scripts are archival inputs, never invoked by
the normal game build or Puffer training. The engine importer is Node/C.

Two instances are installed at the rear reception/service wall and the outside
edge of room 104's front wall. Owners 1173–1174 append after the unchanged 54
ground owners; existing architecture, doors, road and perimeter IDs stay fixed.
All five measured mount points resolve to the existing convex masonry pieces.
Those required supports are serialized, not guessed from proximity after damage.

The body owns eight collision meshes, each at most 240 triangles. Together they
retain all 1,464 source triangles, including open conduit mouths and the housing
cavity. Mesh construction uses centimetre local units and inverse shape scaling
to preserve tiny bevels that Box3D otherwise rejects below its area threshold.
World dimensions, rendering and ballistic thickness remain in metres. The full
shell is used for exit-distance queries and missing-art fallback rendering.

The collision owner uses steel response. The measured 1.2 mm saddle section scales
the existing authored material damage threshold and fracture budget (30/90).
These are game approximations, not measured real-world failure loads. Low-energy
impacts do not accumulate damage; repeated rifle impacts can shed the assembly.
Destroying any required mounting piece, including through a wall charge, removes
its body, all contact shapes and visible model immediately. This increment does
not simulate falling debris, individual screw failures or electrical behavior.

The generic support relationship also handles several required supports and
transitive loss in owner order. A derived first-attachment index keeps ordinary
wall destruction from scanning all the world objects. Portable map version 17
carries dependencies and structural section; cyclic/forward links and snapshots
with unsupported active children are rejected before state mutation. Older wire
versions and replay files are rejected by the existing version guard.

Validation is recorded in `build/swat/review/mounted-props`: Linux/Windows shell
rays, exported-mouth/cover checks, shots/support removal, late replicas, ordinary
input recording/restoration, native PBR/raster/fallback captures and existing
motel/ground/contact/squad/reload/save regressions. Native captures are engine
evidence; the supplied art previews are separate source references.
