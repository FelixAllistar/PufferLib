# Wall hose / capped tap integration

Received 2026-10-12 from the [wall-service packet](https://drive.google.com/file/d/1y4Qjgym4SjlD0-K9PfDPgt1e2pp-51-F/view).
`source.zip` is the unchanged 27,924,231-byte packet, SHA256
`e1007eca9b9eb3f840f68390130487e806e011ff404367ea48d06d88fb321044`.
All 28 packet payload hashes passed. Nested editable scenes, licenses and
authoring scripts remain in this archive; no Python was executed.

The installed hanger uses the original full 8,380-triangle model from
`sources/motel-wall-hose-hanger-v1.tar.gz`, member
`motel_wall_hose_hanger_v1/runtime/wall_hose_hanger.glb`, SHA256
`c9a0bca288bfd4a3c61725e13f21d8b68e6d8eaf8f1b613933cb64034da1b0b5`.
The packet's 2,896-triangle LOD1 stays archival. The capped tap is the unchanged
1,408-triangle `imports/wall_garden_spigot_reuse.glb`, SHA256
`3d5f40625b52004cc9a2d07397a9c3ed951bee4063c7fbf66dad165a2027a89e`.

The full hanger is separated into bracket (712 triangles), rubber hose (5,676)
and couplers (1,992), using the original author's atlas regions and component
bounds. `tools/split_wall_service.cjs` copies each original vertex attribute
byte and embedded image. The independent `tools/audit_wall_service.cjs` checks
that the union contains every original ordered triangle exactly once, including
normals, UVs and tangents, with unchanged PBR metadata and PNG bytes. Native C
`tools/import_static_prop.c` produces the physics arrays; regeneration is
byte-identical. `tools/partition_motel_contacts.cjs` produces 46 bounded contact
subsets with source edge flags; bullet queries retain all 9,788 triangles.

Runtime owners 1332–1335 / assets 55–58 sit on the solid reception-side service
wall, parent 7. The hanger origin is (-8.7, 1.45, -6.091) metres, tap origin
(-9.022, 1.34483493, -6.091), yaw pi, unit scale. Their original relative frame
and 45.003 mm horizontal gap survive. Two bracket screw axes and four tap-back
references resolve to real wall skins. Source screws do not penetrate the wall;
these references are fixed game attachments, not verified structural mounts.

Rubber and metal have separate finite damage owners. Bracket loss drops the
stored hose and couplers; hose loss removes its couplers; the tap has independent
wall support. The authored 8 mm backplate and explicit 3/3/4 mm game health
sections use existing material responses. Brass strength, hose contents,
deformation, mounting loads and plumbing are not simulated. The source tap is
capped and the stored hose disconnected. Source saddle/rib gaps and hose contact
limitations are retained, not certified or silently repaired.

The original three 512px hanger maps are shared across its three parts; the tap
retains its two original 512px maps and has no invented normal or AO map. The
generic motel/storefront loader and draw path now honor glTF normal strength
(0.22 for the hanger), instead of silently replacing it with 1.

Linux and Windows service tests cover exact metre placement, support rays,
open hose center, full ray/exit meshes, rubber/metal damage, support and C4 loss,
authority/live/late replicas, malformed maps and older cabinet-prefix maps.
Expanded contact QA checks 77,542 original triangles/flags and 81,600 ray/exit
samples per platform, repeated after map reconstruction. Native graphics checks
622,384 source/fallback ray/raster samples, PBR maps/factors, original bounds,
shared textures, lighting, owner removal and full texture release.
The normal-strength rendering control changes 4,830 pixels when forced to 1;
restoring the authored 0.22 produces a pixel-identical native image.
All four
ordinary-input completion routes pass on Linux and Windows, with every-tick
replay, mid-run save/resume, fresh replicas and truthful debrief.

Small logs/captures are under `build/swat/review/wall-service`; original source,
runtime models, licenses and regeneration tools are in the normal repository.
This accepts this prop increment, not the full game's performance or behavior.
