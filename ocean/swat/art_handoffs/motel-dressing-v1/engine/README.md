# Motel dressing and shared-wall breach review

Seven delivered props are integrated in each of the four guest rooms, at their
authored metre scale. This folder contains native Windows in-engine captures.

| Prop | Engine support |
| --- | --- |
| Toilet roll holder, robe hook | Bathroom back wall |
| Bucket and service tray | Desk; bucket rests on the tray's 9 mm inset mat |
| Woodland print | Guest-facing bathroom partition above the bed |
| Door viewer | Hinged leaf, replacing the original viewer at 1.60 m |
| Concave bumper | Fixed west wall; binds to a particular drywall piece where applicable |

These small props are decorative children, without hidden box colliders. Removing
their support hides them. The viewer inherits the real hinge transform, while
the bumper stays fixed. The bumper does not impose a physical door stop. The
viewer is an opaque lens, not an optical camera. LOD0 is currently selected;
automatic screen-size LOD is still outstanding.

All seven models use embedded 512 px maps, UV0 base colour/normal/roughness-metal
maps and separate UV1 AO. Metallic/roughness factors and normal scales are read
from the GLBs. Atlas AO clamps; material surface maps retain tiling. The print
uses its supplied full-image artwork UVs. Runtime GLBs are under
`assets/environment/motel_dressing_v1`; editable blends, loose texture maps,
licenses and source/provenance metadata are in the sibling `source` directory.
Source helper scripts, LOD1 alternatives and neutral previews remain in the
complete Drive archives; no Python was executed during engine integration.

## Breachable guest-room walls

Original whole-wall IDs 106–108 are retired in new motel worlds. Their replacement
assemblies are appended after the unchanged original/utility object prefix:
two 12.5 mm board faces, 38 × 89 mm studs at roughly 400 mm centres, segmented
studs, and top/bottom plates. They use the existing finite-charge interaction,
bullet penetration, localized aperture and bounded support-shedding code.
Exterior walls and bathroom partitions remain static mesh geometry.

The matched `motel-breach-0.png` / `motel-breach-1.png` pair shows the actual
rendered physical pieces before and after a breach. This is an initial layered
wall implementation, not arbitrary mesh fracture or structural building collapse.
The original trim on those three full-wall meshes is replaced by the generic
continuous-metre wall materials. Jagged edge detail and richer broken-board
surfaces remain art/rendering follow-ups.

Navigation originally missed a real passage beside the luggage rack because its
60 cm cell centres landed in furniture. Blocked samples now try up to eight
offsets within that cell, resampling physical floors and retaining valid layers.
Every edge still casts the physical body; destruction updates the affected cells.
No scripted route or special teleport is used for the breach.

## Validation

- Linux motel regression: existing nine capsule routes, door operation, open
  rack/basket collision, all 28 support removals and viewer hinge transform.
- With the motel doors wedged, the inter-room route is disconnected before the
  breach and connected afterward; bullet ray and player capsule both pass.
- Map/snapshot reconstruction preserves the same opening and convex fragments.
  Original 147-object and utility-prefix map compatibility is retained.
- Encounter, protocol, tactical, foundation and mission regressions.
- Native Windows motel test and environment graphics checks, including UV1
  buffers, PBR bindings, parent removal, model lifecycle and missing-art fallbacks.
- Normal native `./swat play --mission motel` build/capture.

## Delivery sources

- [Bathroom accessories](https://drive.google.com/file/d/1hRn2xzM4X9-EC_S8R8RG9k0IWnKFFm16/view)
- [Bucket and tray](https://drive.google.com/file/d/1P5RaIIMVbyM781a_HORteRq8EGzSjiyW/view)
- [Wall print](https://drive.google.com/file/d/1Qt5hQXL90Z4a6Tqu4V3BLy-64O5l8jGK/view)
- [Viewer and bumper](https://drive.google.com/file/d/1yIsADquWspYTeiNouz2iLRguVbWTFlTj/view)

Runtime export hashes were checked against the source manifests (the print uses
its SHA256SUMS file). The new art is approximately 9.5 MB of runtime exports and
14 MB of editable/source material. Download archives and extracted duplicate
packages are removed after preserving these files and sharing the proof.

The separate `source/polyhaven_oak_veneer_01` directory holds the artist-selected
1k wood maps and provenance/hash metadata for the next desk comparison. Those
maps are supplied to Slack as source material and are not a runtime replacement.
