# BC_DUMP01 engine intake

Felix approved Blenderbot's handoff in the existing art thread. The downloaded
[source package](https://drive.google.com/file/d/1JjLcKzExcGajLzwcahGANMbtZGAh8E6K/view?usp=drivesdk)
is 18,016,289 bytes, SHA-256
`60887eb3a0986e88fe509684b5a61a7330be3ea22ef1c52caf74c501351e856a`.
All 53 payloads indexed by its SHA256SUMS.json matched after extraction. The
original complete backup remains on Drive; this directory retains editable
source, recipes, provenance, contact notes, selected audit evidence and the
actual-export contact sheet. Runtime has one canonical copy in
`assets/environment/motel_dumpster/BC_DUMP01_closed.glb`.

`node ocean/swat/tools/check_dumpster.cjs` checks original runtime/source/map
hashes, embedded images, index bounds, finite positions/normals/UVs, identity
nodes, ground origin, measured dimensions and 2,908 triangles in five primitives.
Authoring Python is retained as source data and was not executed locally. The
supplied actual-export contact sheet was inspected during intake.

This is a static candidate awaiting engine placement, collision binding and
actual Windows rendering review. It is not loaded into the level yet. Preserve
the authored render mesh and metre-scale ground origin. Choose a supported
service-yard placement, keep routes clear, and validate sparse collision and
material response against the actual shell, skids, fork sleeves and lids.
The source's 64 assembled solids are not one globally welded collider. The lid
nodes do not imply interactive hinges. See DESIGN_AND_CONTACTS.md for scope.

New work is project-scoped original art; the inherited steel/rubber maps retain
their exact CC0 notices. Preserve PROVENANCE.json and source/notices; do not
relabel the complete asset CC0. Native visual and performance acceptance remain
open. Integration should continue autonomously without a manual-playtest gate.
