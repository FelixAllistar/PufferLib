# Flathead screwdriver integration

Received 2026-10-12 from [the MAIN screwdriver packet](https://drive.google.com/file/d/1GumaGbNoyIaW8t-yngWtpEcOui_ouODM/view).
The original 80,716,375-byte outer ZIP had SHA256
`03a6751adb0d386767cc4cf3d3ea7a57aaa9e31d914e3c7d4ab5c9bd6829ac28`;
all 35 payload hashes passed. Compact editable originals, source maps, licenses,
QA and archival scripts are here. The complete original 18-tool archive is
stored once under `../source-tools-pack/`, byte-identical to the packet member.
Redundant outer ZIP and preview copies are excluded. No Python was executed.

Runtime `assets/environment/motel_props/wood_handle_flathead_screwdriver_250mm_MAIN.glb`
is unchanged, 1,700,352 bytes, SHA256
`1ce59f4c3e6befd1ecd204cae6c07bf18169c6047e3f4c2c1fac462a497d0272`.
It retains 2,560 triangles, 1,507 attribute-split vertices, original normals/UVs,
source node transform, one original material, original 1K colour and 512px
packed roughness/metallic. Native C import regeneration is byte-identical.
An independent C image audit verifies all 262,144 packed green/blue pixels against
the unchanged original scalar images. Red is 255 and supplies no AO; there is
no normal map. No LOD, weld, smoothing, decimation or shader retuning was added.

The 250 mm length is the artist's editorial game scale, not a measured tool size.
Source collar faceting and 768 triangles mapped to one UV point remain as supplied;
their mapping intent and physical material boundary are unverified. The original
scale editable scene and complete untouched original source archive are retained.

Owner 1336 / asset 59 appends to the canonical motel without renumbering existing
owners. Unit game scale/yaw and origin (-8.60, 0.7245, -9.35) metres place the tool
on the maintenance trolley. Its actual minimum-height source vertex (1139) rests
on an original top-liner triangle of owner 1175. This establishes the game contact
and fixed dependency, not verified balance or a rolling/dynamic tool simulation.
The new shared surface-contact resolver also preserves existing floor contacts.

Rendering, ballistic queries and eleven bounded contact subsets retain all
original triangles and source edge flags. The narrow tip silhouette stays narrow;
missing art uses the same surviving triangles. The mixed wood/metal assembly uses
one finite steel-cover response and explicit 3 mm game health section. It does not
certify source construction/strength or provide separate handle/shaft destruction.
Sufficient damage removes the tool; losing its trolley support also removes it on
authority and replicas. Handheld use, pickup and prying are not implemented.

Validation evidence is under `build/swat/review/screwdriver`. The maintained
`motel-screwdriver-test` checks source scale/full mesh, real minimum-vertex/liner
contact, silhouette rays, finite partial/removal damage, support loss, live/late
replicas, old hose/tap-prefix maps and malformed-state rejection. Contact QA and
the native `motel-screwdriver` graphics selector cover source geometry/PBR,
art/fallback, authoritative removal and shared texture lifetime. Linux and native
checks pass with 595 silhouette hits/1,324 openings. Expanded contact QA preserves
80,102 triangles/flags and 84,000 ray/exit matches on both platforms, including
encoded-map reconstruction. Native rendering passes 157,428 stable ray/raster
comparisons and complete texture cleanup; the trolley view was inspected. All
four full ordinary-input scenario routes pass on both platforms, including every
tick replay, save/resume, fresh replicas and truthful debrief. The normal players,
servers and float Puffer are rebuilt. Full slice performance/behavior acceptance
remains unfinished; no new performance claim is made under competing League load.
