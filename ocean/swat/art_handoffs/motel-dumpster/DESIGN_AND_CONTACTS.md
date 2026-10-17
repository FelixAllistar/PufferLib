# Design and visible contacts

A roughly 2 m body with fork pockets extending to a 2.486 m total width, on two welded skids. The broad body envelope and sloping lids distinguish the commercial service object from the library's small refuse cans and wheelie bin.

- Body plates: closed thin solids with intentional welded overlaps at corners and floor. This is a static render assembly rather than one boolean-unioned manufacturing solid.
- Top rim: folded/rolled low-poly rails at the sheet edges. Closed lids overlap the rim slightly as a visual overhanging cover; no motion/continuous collision guarantee.
- Fork pockets: two actual open through-sleeves, with four enclosed walls, end-edge lips and two underside gussets each. The empty internal passage is geometric, not a dark plane pasted on a closed block.
- Skids: two closed beveled-end wedge profiles touch Z=0. Their upper sections meet the base at Z=0.20. Underbody clearance is visible.
- Label: 0.37 × 0.26 m on the front slope; its back follows the nominal front sheet plane and can bridge shallow sheet deformation. The main dent is to the right of the label.
- Side caution labels: 2 mm thick, with a 1 mm visual offset beyond the fork sleeve's outer surface. This avoids coincident faces; it is a decal stand-off, not a mounting claim.
- Rear hardware: visible hinge tabs/barrels/pin caps and drain boss. These establish construction visually; detailed bearing seats and internals are outside this static prop's scope.

Retained 64 source solids preserve easy edits. Runtime combines all non-lid solids into Body, and each lid with its own grip pieces into Lid_Left/Lid_Right. This is an editing organization only, with no new engine-owned physical semantics.

Topology qualification: each original source solid is manifold. Joining runtime nodes does not boolean-union these solids. Welding every coincident runtime position at 1 micrometre merges two shared sheet-boundary edges to valence 4; the consolidated body is therefore not a globally manifold welded solid. These are intentional assembled-panel contacts, not holes. Use renderer geometry as supplied, not a globally welded manufacturing mesh.
