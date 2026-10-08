# Connected road and ground engine integration

The original identity-root render and collision GLBs are installed without
transforming or reshaping their geometry. The runtime manifest is retained
verbatim. Numeric ownership is now assigned by the engine: support_matrix
entry i maps to owner 1119+i, through 1172. The old court owner 0 and 75
perimeter components remain unchanged. Each new owner has one closed 12-triangle
mesh, fixed support and its exact matching visible mesh.

The importer verifies both original GLB SHA-256 identities and the placement
manifest hash, welded oriented closure and matching render/collision triangles.
It recentres each physical mesh around its owner while preserving world-space
vertices. Rendering retains identity placement. Model bounds are checked against
all 54 physical owners in native graphics QA. Removal hides only that owner;
missing render art uses the exact closed physics triangles.

The road is 8 m wide, with a 6 m driveway. Supported ground is bounded by
X [-64,64], Z [-48,52], with original court/shoulder cuts and 2 m transition
grades. Staging, extraction and the outward clear corridor remain usable.
The existing raised bank/lip and rocks are preserved above their support; seam
probes explicitly query the supporting court/shoulder/new-ground beneath them.
Actual controller crossings go around this cover instead of treating it as a
flat seam. Unsupported coordinates outside the finite envelope stay unsupported.

Original Clean Asphalt and Gravel Ground 01 PNGs/PBR factors and authored metric
UVs are retained. Asphalt uses the current CONCRETE physics/acoustic profile,
with SOIL for gravel. Ground remains static and indestructible; no physical
craters, loose gravel, far landscape or finished roadside dressing is claimed.

Navigation retains the prior indoor 60 cm sample arithmetic and extends it to
216 × 168 cells with four height layers. Other worlds keep their smaller grid.
Grid, dirty marks and 32-bit BFS scratch share a single lazy heap allocation,
so map/reset/close ownership remains simple and wide routes cannot overflow
the Windows stack or truncate indices above 65535. A four-level physical slab
fixture exercises actual high-layer route indices. Incremental/full cache
comparison now copies the graph contents rather than its pointers.

Linux and native Windows physical ground checks pass: 740 supported/seam rays,
closed thickness, live court/driveway/road/end-grade crossings, real squad road
movement, finite boundary queries, wire-map collision reconstruction, rejected
modified maps and reset/close. Existing Linux motel, perimeter, encounter and
3D stair/navigation regressions pass. Native GTX 1060 graphics QA passes
56,250 selected collision/raster samples, all 54 owner bounds, original PBR maps,
independent removal and fallback. The three included images are native engine
captures, not Blender previews. Slack review: F0C7RRC86ER.

The setting remains sparse and the broad gravel too pale. A coordinated visual
revision is underway; rock silhouettes must keep render/collision agreement.
Protocol and replay version is 16; regenerate older recorded sessions.

## Full original backup

The repo retains the runtime, packed editable Blender source, provenance, QA
records and native proof. The original full-packet manifest/SHA list is retained
verbatim and refers to its original archive paths; it also covers authoring
scripts and offline previews not needed by the runtime. The complete backup
remains available as the original [source archive](https://drive.google.com/file/d/1dnryjU8Vll9yl_bxUbvgSG0AboMrMqzy/view),
[review archive](https://drive.google.com/file/d/1NKlUhQUk1FIAflwDZrYHHiUsMU9ooL47/view)
and [runtime archive](https://drive.google.com/file/d/1rYivUaRJPWQo23ZtkPRBRXUYo3pGlf3J/view).
No Python authoring scripts were executed during integration. Verified duplicate
local runtime/source staging files were removed after retaining these repo copies.

Full front/exterior/inter-room completion, exact every-tick replay, mid-run
save/resume and fresh-replica debrief pass on Linux and native Windows after
integration. All three runs finish with two arrests, three civilian rescues,
both weapons recovered, squad regroup and no protected deaths/unlawful force.
Generated two-storey movement, encounter and real-UDP regressions also pass.
The ordinary `./swat` native motel capture and refreshed Windows player build
pass on the GTX 1060. This is controller QA; full-slice human/co-op acceptance
and learned-policy quality remain separate work.
