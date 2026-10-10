# Separate window revision R1 receipt

Received 2026-10-10 from the [DataDyne art thread](https://datadynedesigngroup.slack.com/archives/C0C5TN3AA8K/p1791655763628429). [Original Drive packet](https://drive.google.com/file/d/1TUsi3A2jnaCMzoVTja72zI0WgGJyvs6j/view).

Archive: 9,445,395 bytes, SHA-256 `3b720a663784e7489bf5807d1ff9dea0ef21183a8bea01a0790af828cc411f6b`. All supplied SHA256SUMS entries passed. The archive retains original Blender source, both GLBs, maps, component/physical/support mappings, diagrams, previews, review reports, scripts and scoped CC0 license. Archival Python was not executed.

The engine's independent Node audit reads the actual GLB buffers and complete scene-node transforms. It checks exact primitive/material assignments and non-overlapping full coverage of all 10,212 triangles; all 349 mapped component bounds agree with the manifest within 2 micrometres. Every component's welded edges have two incident triangles, and all six panes measure 6.4 mm. Physical-parts rows agree with the runtime mapping. This is a geometry/mapping check, not a structural certification, self-intersection check or runtime rendering/collision acceptance.

Reproduce after extracting the archive:

```bash
node ocean/swat/art_handoffs/motel-window-r1/audit.cjs /path/to/briar_court_windows_realistic_r1
```

Room window: 108 source components, 5,280 triangles, seven meshes, six materials. Lobby facade: 241 components, 4,932 triangles, eight meshes, five materials. The original window assets are still the installed runtime version. Next integrate grouped material/support components with matching render/collision removal and preserved original legacy prefixes; do not create one physics body per source component or silently replace art while retaining the old collision. The archive's 518 proposed support relationships still require engine contact validation and grouping.
