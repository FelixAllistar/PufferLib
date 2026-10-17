# BC_DUMP01 engine integration

The unchanged approved GLB now stands beside the maintenance shed at engine
origin **(-4.9, -0.08, -8.4) m**, yaw π. Its eight authored skid-bottom vertices
contact the existing closed court foundation. Moving the assembly 0.6 m east
from its first candidate clears the original service-exit capsule route. The
model, UVs, source transforms, maps and editable Blender source remain unchanged.

The original body node has 1,964 triangles; the two closed lid nodes have 472
triangles each. Three runtime owners retain the full sparse source geometry,
including open fork sleeves and the space between the skids. Thirteen cooked
mesh subsets contain at most 240 triangles apiece and retain source adjacency.
The missing-art path draws that same mesh, rather than its enclosing box.

The body uses the existing steel game response. Each lid uses the existing
rubber response as an approximation for its molded material and depends on the
body. A destroyed lid exposes the original interior while the other lid remains;
body removal removes its dependants. These are static storage props, with
finite authored health, not interactive lid hinges or certified material
strengths. Source assembly/manifold qualifications remain in
[DESIGN_AND_CONTACTS.md](DESIGN_AND_CONTACTS.md).

`tools/import_motel_dumpster.cjs` verifies the original hashes before emitting
`motel_dumpster_data.h`. It preserves all 2,908 triangles and four scalar PBR
materials and derives the eight actual minimum-height skid vertices. No local
Python or Blender authoring recipes are executed. Original rights and notices
remain in the unchanged provenance/source files; inherited CC0 maps do not
make the complete asset CC0.

`make -C ocean/swat motel-dumpster-test` covers thin-panel queries, fork and
underbody openings, physical service-lane walking and container blocking,
original service-exit clearance, damage, reset, encoded late replicas, older
canonical map prefixes and malformed geometry/support rejection.
The bullet fixture sends the same round through the fork opening to the actor
behind it and stops that round at the original thin steel panel. Twenty-seven
encoded states, including those impacts, agree on Linux and Windows.

`make -C ocean/swat motel-dumpster-art-test-build` builds a small graphics check.
Pass an existing output directory and an asset root containing only
`motel_dumpster/BC_DUMP01_closed.glb`. The Windows build provides the equivalent
`test_motel_dumpster_art.exe`. This isolated setup loads only the four embedded
maps, sharing them between all three owners. It checks original materials,
front/top art and fallback silhouettes against physical shapes, native lighting,
lid removal and texture release. The surrounding yard is the ordinary graybox
fallback, so these views accept the prop and its placement rather than the
finished motel composition.

The acceptance receipt is [ENGINE_INTEGRATION.json](ENGINE_INTEGRATION.json).
Selected native captures are retained in `engine_previews/`; complete local
logs, frames and journals are under `build/swat/review/dumpster-integration`.
Protocol/replay **v30** requires matched tools and fresh recordings/saves.
Three complete ordinary-input motel approaches pass replay, save/resume and
fresh-replica checks on Linux. Windows replays all 44,996 frames exactly: squad
restraints through the front doors, and hostile-fire exterior/inter-room charges.
All objectives finish with no officer/civilian deaths or unlawful force; each
charge route has one arrest and one armed suspect lawfully killed. These runs
use fixed seed 81, so they do not accept varied-seed or learned-policy behavior.
Heavy performance testing remains held. Whole-slice visual, varied-seed,
training/policy and stable-load performance acceptance remain open.
