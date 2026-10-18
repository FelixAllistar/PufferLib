# Roadside engine candidate

The approved BC_SEDAN01 and BC_SHELTER01 exports retain their original geometry,
UVs, materials, textures and transforms. Their original handoff manifests and
provenance remain unchanged. Engine ownership is generated separately in
[roadside-engine-ownership.json](roadside-engine-ownership.json).

| Candidate | Placement origin, metres | Facing | Physical owners | Source triangles |
| --- | --- | --- | --- | --- |
| Parked sedan | (-6, -0.08, 6.65) | -Z, toward the guest rooms | 50 | 13,124 |
| Waiting shelter | (-8, -0.08, 34.75) | +Z, toward the connected road | 28 | 7,868 |

The sedan's eight authored tire-bottom points seat the parked assembly on the
court foundation. Four independent shelter posts retain their original
baseplate bottom contacts on the existing shoulder soil piece. No extra
invisible platform or volume fills the shelter, wheel arches or bench gaps.

Every delivered solid is matched to its source audit bounds. Component groups
retain independent steel, rubber, opaque glass and timber response. Timber slats
own their steel fixings; posts support the roof frame, rear frame and dependent
parts through the ordinary attachment graph. Existing game material/health
sections approximate these static assemblies. They do not certify fabrication
strength. The sedan keeps its supplied exterior-only closed coachwork volume;
vehicle panel-stack ballistics and suspension remain unqualified.

Physical contact subsets contain at most 240 cooked source triangles each, with
original adjacency. Full source meshes provide sparse ray/thickness queries.
The renderer submits one shared model bank per assembly through its first
surviving owner. Damage masks rebuild filtered geometry when ownership changes;
source textures, normals and UVs remain borrowed from the original GLB. The
missing-art fallback draws the same physical meshes. Both GLBs are copied into
the normal Linux/native runtime asset trees during installation.

`node ocean/swat/tools/check_roadside_candidates.cjs` verifies all original
indexed payloads. `node ocean/swat/tools/import_motel_roadside.cjs` regenerates
the engine descriptors and triangle ownership. These commands use Node and
never execute the preserved Blender/Python recipes.

`make -C ocean/swat motel-roadside-test` exercises real contact openings,
ordinary controller movement from the court to the shelter, ballistic screen
attenuation and its damaged aperture, independent part/support removal,
encoded replicas, canonical map rejection and reset. The graphics check built
by `motel-roadside-art-test-build` compares actual loaded triangle ownership
and source/fallback raster silhouettes with physical ray casts, before and
after damage. It also captures production lighting and checks resource release
and immutable render authority. Supply an output directory and an isolated
asset root containing only the two original exports.

Protocol/replay v35 includes the additional physical objects. Older canonical
map prefixes remain supported; matching peers and new mission journals/saves
are required. Map reconstruction can service transport between geometry chunks.
Current-state publication advances network revision without advancing gameplay.
The real-UDP slow-load case delays construction beyond six seconds, requires
a reliable ping acknowledgement and verifies a tick-zero replica under the
unchanged shipping deadlines.

Final cross-platform mission replay exposed a one-ULP difference in the sound
position when trying a blocked door handle. That feedback now belongs to the
leaf centre, matching ordinary open/close cues. Aimed contact still decides
whether the door can be reached; lock, wedge, charge and interaction rules are
unchanged. Ordinary press/hold checks cover locked, wedged and charged doors
at two aim heights, with one cue and no state/tool consumption.

This is an engine candidate. Software-rendered correctness checks do not prove
native Windows visual quality, stable-load responsiveness or frame-time
acceptance. Those checks remain open under the machine-use constraint, alongside
vehicle-cover qualification and the broader vertical-slice acceptance work.
