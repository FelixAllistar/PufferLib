# Separate window revision R1 receipt

Received 2026-10-10 from the [DataDyne art thread](https://datadynedesigngroup.slack.com/archives/C0C5TN3AA8K/p1791655763628429). [Original Drive packet](https://drive.google.com/file/d/1TUsi3A2jnaCMzoVTja72zI0WgGJyvs6j/view).

Archive: 9,445,395 bytes, SHA-256 `3b720a663784e7489bf5807d1ff9dea0ef21183a8bea01a0790af828cc411f6b`. All supplied SHA256SUMS entries passed. The archive retains original Blender source, both GLBs, maps, component/physical/support mappings, diagrams, previews, review reports, scripts and scoped CC0 license. Archival Python was not executed.

The engine's independent Node audit reads the actual GLB buffers and complete scene-node transforms. It checks exact primitive/material assignments and non-overlapping full coverage of all 10,212 triangles; all 349 mapped component bounds agree with the manifest within 2 micrometres. Every component's welded edges have two incident triangles, and all six panes measure 6.4 mm. Physical-parts rows agree with the runtime mapping. This is a geometry/mapping check, not a structural certification, self-intersection check or runtime rendering/collision acceptance.

Reproduce after extracting the archive:

```bash
node ocean/swat/art_handoffs/motel-window-r1/audit.cjs /path/to/briar_court_windows_realistic_r1
```

Room window: 108 source components, 5,280 triangles, seven meshes, six materials. Lobby facade: 241 components, 4,932 triangles, eight meshes, five materials.

Installed in the current motel recipe as four room windows and one lobby facade. Original source dimensions, winding, materials, UVs and maps are retained. Each room assembly has 24 damage groups; the lobby has 41. These 137 appended objects preserve all 26,052 installed triangles in 194 contact shapes, each bounded to 240 triangles, with full exact ballistic queries. Twelve opaque panes retain their measured 6.4 mm depth. The old opening owners remain logical anchors with no triangles or collision; the old ten panes are inactive. The 1,190-object map prefix still reconstructs its original windows. Partial revised recipes and modified material sections/supports are rejected.

Reproduce the importer after extracting the original archive:

```bash
node ocean/swat/tools/import_motel_windows.cjs /path/to/briar_court_windows_realistic_r1
```

The checked-in engine-groups.json records all source-component membership, logical damage sections, primitive ranges and dependencies. The packet's 518 adjacency relationships guide auxiliary grouping; their geometric contact tolerances have not been independently qualified here. Root mounting uses the existing opening owners as virtual game anchors. Dependencies and aluminum/rubber/cloth strength tables are explicit game approximations, not certified load ratings or whole-building collapse. Loss of a load member removes dependent render and collision groups together. Thin opaque cloth blocks sight while ordinary bullets penetrate it; several bullets or failed mounting can remove the whole grouped panel. Glass remains opaque rather than gaining an unverified transparency behavior.

Original authored packet, source and scoped license remain archived; runtime GLBs are the two unchanged originals under assets/environment/motel_windows_r1. No archival Python was executed. Existing input replay regenerates the current canonical recipe; keeping the legacy map prefix does not make old engine-version input replays compatible.

Engine validation: the C window checks cover exact triangle preservation/contact bounds, two-face pane/exit casts, source sections, independent glass destruction, frame/socket cascades, thin-curtain penetration into a concealed actor, local assembly C4 and encoded late replicas. Legacy recipe validation and ordinary canonical-spawn pane shooting/save restore also pass. Native graphics match 76,770 stable source/fallback ray/raster samples, including original mesh/material counts, shared texture release and production lighting/shadows. Logs and captures are under build/swat/review/window-integration.

The existing native performance tool was run for 180 indoor shooting frames with three squad bots and nine actors: simulation mean 3.871 ms (p95 6.109); total mean 43.774 ms (p95 104.926), including variable render/driver work. Concurrent local workloads and compilation were present, and no matched pre-R1 baseline was collected: these figures do not establish a regression or a stable performance budget. Audio was disabled in this measurement. Rendering cost remains work to improve.

Full scenario checks pass on Linux and native Windows through hostile-fire front, exterior-masonry and inter-room charge approaches. These drive ordinary player inputs and verify arrests, civilian evacuation, evidence, squad regroup, exact every-tick replay, mid-run save/resume, fresh replicas and no contact-buffer overflow. The broader motel C suite still checks blocked masonry penetration/civilian protection, live squad breach movement, fence C4, nine capsule routes and legacy map reconstruction. The normal ./swat native-player capture was inspected after rebuilding Linux player/server/replay, native player/server/replay and normal Puffer.
