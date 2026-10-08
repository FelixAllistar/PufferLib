# Motel masonry integration — 2026-10-08

The imported exterior was an indestructible mesh tagged plaster: unbreachable
but cheap to penetrate. It now has a material-aware masonry core and original
textured art clipped to its actual live convex sections.

## Geometry and material behavior

- Facades, rear, end and service doorway walls use 180 mm masonry. Bathroom
  partitions use gypsum board; guest-room party walls retain skins and studs.
- Door/window apertures come from authored triangles. Their boundaries stay fixed.
  Shared deterministic fracture vertices produce oblique internal boundaries.
  They tile without gaps and vary by parent/patch; no extra objects were added.
- Each section is an extruded convex polygon in both physics and rendering.
  Clipping interpolates original UV0/UV1 and normals. Cached art keys include
  the actual polygon. A reduced-depth matching core supplies the exposed depth.
- Exposed masonry/plaster strips follow shared surviving/removed polygon edges,
  including partial overlaps at architectural patch boundaries. Their +Y axis
  points into surviving material. Fine edge detail remains cosmetic.
- Current bullets and the ram cannot accumulate a masonry doorway. Charge
  resistance uses material and core thickness; substantial steel and concrete
  resist the current charge. Thin board can pass reduced-energy shots without
  removing a person-sized opening. Actor role never changes wall resistance.
- Bounded support shedding applies only to framed assemblies. Mounted details
  follow the actual supporting polygon. Furniture behind the hole remains solid.

## Authority and art handoff

Canonical parent identities and the 1,044-object layout count are retained.
Sections transmit explicit convex boundaries through the ordinary map protocol.
Network/replay version 13 marks this recipe change; old journals are rejected.
Explicit legacy prefix map reconstruction remains tested.

`layout_tool motel-walls` now exports format 2: source_xy_bounds are only bounds;
source_xy_polygon gives the four actual source-space boundary vertices. The
edge records include world origin, yaw, roll, length and physical core depth.
`wall-geometry.json` retains the tested rear-wall example. Linux and native
Windows exports match after parsing JSON.

## Evidence

Matched native Windows `motel-masonry-0.png` / `motel-masonry-1.png` show intact
and breached rear wall. The sink behind it remains an obstacle. The edge closeup
shows the angled core/strip joins; `room101-v4-row.png` checks intact facade art.

- 16,182 bidirectional samples across solid rear/end walls find no gaps. There
  are 734 oblique fragment edges in those sampled assemblies.
- 19,856 native GPU samples on both faces of an isolated irregular fragment
  match exact convex ray casts: 10,934 filled and 8,922 empty. Samples within
  two pixels of an edge are excluded for raster coverage.
- All weapon profiles protect a civilian behind intact masonry, with original
  material health; ram resistance and local charge/capsule/replica checks pass.
- Real human/squad controllers traverse exterior and inter-room charge openings;
  existing door, furnished-route, civilian evacuation and support tests pass.
- Linux/Windows motel and foundation save/replay checks pass, including 1,500
  queued squad-entry replay ticks. Linux real UDP checks pass.
- Full native Windows graphics regression and ordinary `./swat` capture pass.

Compact results are in validation.txt; full logs and intermediate captures are
in `build/swat/review/masonry-fracture`. The early WSLg silhouette check passed;
its broader duplicate graphics run was stopped once native verification began.

## Limits

Strength and fracture boundaries are authored approximations, not engineering
or blast predictions. Pieces are still coarse; the opening can read as a bent
panel cut rather than chipped masonry at close range. More varied local damage,
small chips, physical debris and structural support/collapse remain unfinished.
Concrete floors/roofs/piers and most furniture are still static. This subsystem
validation does not establish full scenario completion or the finished slice.
