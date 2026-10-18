# Design and contact notes

## Design intent

A quiet, believable anonymous compact, suitable for a motel parking space without becoming a hero vehicle or resembling a branded recreation. Four doors are legible through paired handles, A/B/C-pillar alignment and continuous shut lines. The curved shoulder, hood/deck slope, crowned roof, real arch cutouts, tread shoulders and recessed wheel-cover slots carry the silhouette. Small markers, fuel flap, wipers and blank panels remain subordinate.

## Geometry and attachments

- Body: one closed longitudinal shell with continuous longitudinal/perimeter UVs. Wheel cutouts rise through the lower sidewall; the narrow central underbody remains continuous.
- Cabin: shaped opaque metal backing, closed glazed overlay panels and restrained seals. Frames intentionally overlap by approximately 1–2 mm where needed for a sealed static exterior; they are not Boolean-unioned manufacturing parts.
- Door/filler/hood/boot gaps: small closed strips fitted by ray casts to the actual exported source surfaces. They are shading and silhouette detail, not operable door splits.
- Lenses, grille, plate blanks and bumper: closed surface pieces seated into the swept fascia. The bumper/body split and rub strip are visibly distinct.
- Tires: four independent closed profiled rings. Each tire's bottom is exactly Z=0 in Blender / Y=0 in glTF. Wheel centers are 0.315 m above the plane. The tire-to-coachwork gap is geometric; no fake shadow is used to imply attachment.
- Wheels: opaque covered dishes and small lug heads. The inner dish and short axle hints support underside review, without implying mechanical completeness.
- Mirrors: compact closed housings, small stalks and opaque reflective faces. They set the maximum width of 1.960 m.

Small closed solids deliberately intersect at assembly joints. Source-level closed-solid topology and runtime spatial-weld diagnostics are both recorded; surface intersection is not interpreted as a physical support or destruction graph.

## Placement

Place the root at the desired road/parking surface height. Rotate about glTF +Y. Front is glTF +Z. The root is centered between wheel contact pairs. Do not treat the wheel child origins as animated pivots. Do not apply the Blender-to-glTF axis conversion again after a standards-compliant glTF import.

No collision or pathfinding envelope is supplied. Allow for both mirrors when planning clearance. A scene integrator may choose a separate coarse collision envelope; this package does not decide gameplay blockage or cover strength.
