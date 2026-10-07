# Briar Court Room 101 materials and detail

The eight original GLBs from felixallistar-dot's 6 October handoff replace only
assembly instances 010 (walkway), 012 (facade), 015 (door), 018 (plaque), and 020
(sconce). The other motel instances retain their original assets. Their compiled
collision, logical door slab, damage IDs and hinges remain authoritative.

Three decorative overlays follow their respective facade, door and plaque in
normal, shadow and secondary camera draws, and disappear when the parent does.
The supplied GLB hierarchy is loaded without resizing or recentering.

The renderer restores scalar glTF material factors, including omitted metallic
factor defaults, and each material's normal strength. Normals use the signed UV
derivative basis; packed textures use G roughness and B metalness. Linear color
factors are encoded for the engine's existing gamma-2.2 decode. This is the
engine's current approximation, not a new calibrated lighting pipeline.

`SWAT_MOTEL_ROOM101=0 ./swat play --mission motel` selects the original room for
comparison. The default enables the revision when its entire package is present.
Models load with the motel and unload when changing locations.

Source: https://drive.google.com/file/d/1OsWdtrOmgvHDr49SPdDNF9k2jqGpOV1p/view
Original hashes, material instructions and CC0 licenses are alongside these files.
The full source handoff and offline review are retained under
`ocean/swat/art_handoffs/room101-realism-v2`.

Integration checks on 7 October passed on native Windows / GTX 1060 and WSL /
D3D12: material factors/maps, transformed bounds, matched before/after captures,
0/45/100-degree door poses, parent removal, unchanged world state and unload/reload.
Windows lighting/shadow regressions also pass. Matched engine images and the
short render timing check are in the full handoff's `engine` directory. Fine
trim/shadow edge aliasing remains visible; this is ready for visual review, not
final art approval. Original delivered files and their hash manifest are unchanged.
