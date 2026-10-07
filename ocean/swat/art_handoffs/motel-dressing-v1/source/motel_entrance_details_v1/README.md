# Two motel entrance details

Original CC0 door-viewer face and concave wall bumper. Four GLBs (2 designs × 2 LODs), Blender source, original 512 px PBR maps, independent UV1 AO, rebuild scripts and QA included.

For small on-screen room-distance use, LOD1 is the budget candidate: viewer 562 triangles, bumper 914. LOD1 has visible metallic-highlight faceting in macro close-ups; use LOD0 for closer inspection: 976 and 2,044 triangles respectively. The viewer is 25.4 mm wide; the bumper is 63.5 mm wide. Both use rear contact-center pivots, metre units, glTF Y-up.

Viewer belongs to a door leaf; bumper belongs to a fixed wall. They are nonfunctional visual details. Nothing changes Room101, physics, collision, door motion, locking, optics, or engine files.

- ENGINE_IMPORT.md: axes, placement, ownership, PBR, LOD and rebuild contract
- MANIFEST.json: measured runtime dimensions, counts, hashes and checks
- CATALOG_CANDIDATES.json: 2 designs + 2 LODs, ready for catalog review
- REFERENCES.json: vendor-photo and dimension research; no third-party images bundled
- previews/: neutral GLB reimports, individual details and separate mount vignettes
- REBUILD_QA.json / TEXTURE_REBUILD_QA.json: independent byte-exact results

Previews are Blender renders, not game captures. Mount vignettes establish surface ownership, not approved placement in the actual room. Art review and engine integration remain separate decisions.
