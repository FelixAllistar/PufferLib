# Roadside sign integration

Drive handoff 1zAefVHDuw5KTl_V96BebrOOZJqLhoDHL, all delivered SHA256SUMS verified.
Editable packed Blender source, original art license, DejaVu font/license, print
textures, placement/provenance and compact geometry QA are retained above.
The runtime GLB is `assets/environment/motel_roadside/roadside_sign.glb`:
SHA256 90751c47351f7e171f2f9123726d4582853e51bb90db22f92b4b6806f21b650c.
The baseline remains in the existing motel source/catalog; no extra baseline copy.

Rendering replacement only: asset 14, engine owner 140, source instance 139.
World root (9.7,-.08,8), yaw -.13 radians, scale 1. Original collision remains.
Seven opaque mesh primitives have 319 triangles (previously 3,075). Native tests
check mesh count, triangles, bounds equality to the original, visibility, owner
removal and location teardown. No separate invisible gameplay object is added.

Native parking view reviewed: BRIAR COURT MOTEL / VACANCY is readable, original
pole/base/casing extent retained. Full environment graphics regression and normal
player capture pass. Source pack build scripts were not executed locally.
