# Motel masonry integration — 2026-10-08

The imported exterior was an indestructible mesh tagged plaster. That made a
wall unbreachable but cheap to penetrate. It is now a masonry core with original
textured art clipped to the same live physical sections.

- Source core is 180 mm; facade door/window apertures come from its authored
  triangles. Trim does not become a solid box across openings.
- Facade, rear, end and service doorway walls use masonry. Bathroom partitions
  use gypsum board; the existing guest-room party walls retain skins and studs.
- Material impact thresholds prevent current bullets and the ram accumulating
  a doorway through masonry. Charge resistance scales by actual core thickness.
  Concrete and substantial steel exceed the current game charge's capacity.
- Penetration still depends on material and traversed thickness. Thin board can
  pass a reduced-energy round without a person-sized hole; actor role does not
  change wall resistance.
- The support-shedding rule only applies to framed assemblies, preventing a
  local masonry breach from deleting the whole wall.
- Canonical parent IDs remain stable. New objects carry explicit material,
  geometry and group through the normal map/snapshot protocol. Mounted wall
  details disappear if their local supporting section disappears.
- Motel navigation now covers the west exterior/reception approach, previously
  outside the fixed grid. Grid resolution and physical edge checks are unchanged.

## Evidence

`motel-masonry-0.png` and `motel-masonry-1.png` are matched native Windows lit
captures before/after a rear-wall breach. The sink remains a real obstacle behind
that opening. `room101-v4-row.png` checks intact facade art and openings.

Linux regression logs accompany this review. The motel test checks all twelve
current shot profiles against a civilian behind masonry, full material health,
ram resistance, local removal with surviving wall, capsule clearance through
the wall assembly and its network replica, and a subsequent unoccluded hit.
A separate west-side entry checks the full-world capsule sweep and navigation.
Existing motel routes, door states, dressing attachments and legacy maps pass.
The simulation test also compares thin-board penetration for suspect/civilian
roles. Tactical, protocol, encounter and mission checks pass.

The native environment graphics test passed original-art clipping/removal,
material/UV handling, retained openings, resource lifecycle and missing assets.
The normal native Windows player was rebuilt from these sources.

## Limits

Strength values are authored game approximations, not engineering/blast data.
Breach edges remain rectangular sections, not arbitrary jagged fracture or
building collapse. Concrete floors/roofs/piers and most furniture are still static.
A hole behind furniture does not erase that furniture. The intact art and
colliders are aligned, but irregular chipped plaster, exposed masonry texture,
and debris need a subsequent pass.
