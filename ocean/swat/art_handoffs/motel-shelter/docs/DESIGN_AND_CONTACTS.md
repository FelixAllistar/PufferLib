# Design and art contact notes

This file describes the modeled visual relationships. It is not a fabrication drawing, engineering analysis, installation instruction or accessibility assessment.

## Intended reading

A small, maintained-but-weathered roadside waiting shelter. The low mono-pitch sheet roof, muted green steelwork, pale aged timber and restrained chips fit the nearby service-edge art family. It is a freestanding candidate: the engine chooses whether and where it belongs. No pavement, curb, street scene, transit route or sign is baked into it.

## Seats, screen and supports

- The four seat slats span X −1.20 to +1.20 m, top Z 0.470 m, with small gaps between them. Their front edge is Y −0.02625 m and rear edge Y 0.41625 m.
- Three transverse bearers at X −1.03, 0 and +1.03 m touch the slats' undersides. Their rear ends enter the rear support rail. Diagonal braces run from the front underside back into the low rear frame.
- The backrest's three boards are slightly reclined. Three matching steel uprights sit against their rear surfaces and meet the seat-support assembly. Small modeled screw heads enter each board at those supports.
- End arm rails join the outer seat assembly and have rounded edges. There is no center divider or claimed passenger capacity.
- Two folded, physically thick screen panels cover the rear from Z 0.255 to 2.010 m. Their shallow ribs are part of continuous solid sheets, not open slats. The center mullion overlaps the seam; it is offset from the crossing rails to avoid coplanar face fighting. Clips and rivets connect the panels visually to the rear rails.
- The gap below the low frame and the opening above the screen are intentional. The art is not represented as a sealed weather enclosure.

## Roof, frame and grade

- The roof falls toward the rear: low sheet surface Z 2.360 m at Y −0.800, Z 2.208 m at Y +0.720. That is a visual pitch of approximately 5.71 degrees. Corrugation rises another 19 mm.
- Four corner posts meet the roof rails; short diagonal knee braces and side gusset plates articulate those connections. Crossmembers and purlins meet the roof underside. Thin sheet and frame overlaps are intentional seated joins.
- Rear runoff enters an open-top U gutter. Its floor has a true through-opening into a hollow pipe; the pipe continues down the rear-right side to an open bent outlet. Socket and clamp pieces are modeled. There is no fluid simulation or drainage-capacity claim.
- Four broad baseplates have lowest vertices at Z 0. Each receives a post collar and four small washer/nut assemblies. There are no floating footings or subterranean foundation meshes. Anchor fasteners are static visual hardware; no terrain anchoring or load behavior is implied.
- The overall envelope includes roof overhangs, gutter caps and the outlet, not just the four post centers.

The final art pass recesses the crossing roof rail ends, slightly offsets the verge and gutter cap exterior faces, and seats the lower mullion and clip ends inside their supporting pieces. A same-facing coplanar-triangle screen found no cross-object coincident surface patches in the final source. This is an offline art check; general collision and near-coplanar behavior in a specific engine are outside that result.

## Basis, pivots and anchors

Runtime mesh nodes `BC_SHELTER01_FrameAndScreen`, `BC_SHELTER01_RoofAndHardware` and `BC_SHELTER01_BenchTimber` have identity transforms and share the ground origin under `BC_SHELTER01_ROOT`.

Blender source coordinates are X right, Y toward rear, Z up. Standard glTF coordinates after export are (X, Z, −Y). The asset's front is therefore +Z in glTF. Custom prose/array metadata is labeled by basis; normal node transforms are basis-converted by the exporter.

Eight anchors are descriptive node transforms:

- Four `ANCHOR_Foot_*` nodes at the ground-contact plate centers: Blender X ±1.38, Y ±0.58, Z 0
- `ANCHOR_SeatCenter`: Blender (0, 0.195, 0.470)
- `ANCHOR_RoofFrontCenter`: Blender (0, −0.800, 2.360)
- `ANCHOR_RoofRearCenter`: Blender (0, 0.720, 2.208)
- `ANCHOR_DrainOutlet`: Blender (1.430, 0.860, 0.125)

No anchor establishes an animation pivot, navigation target, seated actor transform, cover classification or collision shape. Full exact exported transforms are in `MANIFEST.json`.

## Materials and topology

- Weathered green steel: original 1024 px basecolor atlas plus packed roughness/metallic artwork. Pigment wear is concentrated at sheet seams and exposed edges; no light or shadow is painted into the texture.
- Galvanized roof/fixings: exact inherited 256 px steel basecolor, scalar metallic 0.78 and roughness 0.66.
- Timber: original 1024 px eight-board atlas with lengthwise grain and separate roughness; metallic 0.

All three use opaque, single-sided core glTF metallic–roughness PBR. The runtime has no external texture URIs. Source geometry remains separate, named and editable. Consolidated runtime contact overlaps are intentional; do not interpret it as one globally welded watertight object or derive engineering semantics from its topology.
