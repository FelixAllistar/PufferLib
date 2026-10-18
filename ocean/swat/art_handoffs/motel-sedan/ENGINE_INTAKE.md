# BC_SEDAN01 engine intake

This directory retains the unchanged approved source, recipes, provenance,
export previews and audit records from Blenderbot's public handoff. Runtime has
one canonical copy at `ocean/swat/assets/environment/motel_sedan/BC_SEDAN01_parked.glb`. All original indexed payloads verify;
the auditor resolves the original runtime entry in SHA256SUMS.json to that
canonical file. The complete archive remains on Drive, with its bytes and hash
recorded in [ENGINE_INTAKE.json](ENGINE_INTAKE.json).

`node ocean/swat/tools/check_roadside_candidates.cjs` checks indexed payload hashes, original GLB bytes, finite indexed
positions/normals/UVs, triangle/material inventory, embedded PNGs and grounded
metre bounds. The supplied actual-export contact sheet was inspected. Authoring
recipes are preserved as source data; no local Python or Blender authoring was
executed. Source geometry, maps, UVs and transforms remain unchanged.

The sedan is a static exterior with opaque windows and four authored wheel meshes. Their shared origin is a placement origin, not a working wheel rig. Interior, driving and interactive doors are outside this source candidate.

The scene does not yet place this candidate. Engine integration must bind
matching sparse collision/material behavior, keep routes clear, verify support
and destruction, and inspect actual native rendering. Performance acceptance
remains open under the machine-use constraint. Continue this work through
automated checks without requiring manual playtests between increments.

New work is project-scoped original art. Preserve [PROVENANCE.json](PROVENANCE.json)
and all inherited notices; the complete asset has no blanket CC0 dedication.

The later [engine candidate integration](../ROADSIDE_ENGINE_INTEGRATION.md)
records placement, physical ownership and validation scope. This intake record
retains the original pre-integration status and source evidence.
