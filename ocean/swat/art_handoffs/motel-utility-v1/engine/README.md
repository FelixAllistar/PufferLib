# Motel utility props — engine integration, 2026-10-07

The wooden luggage rack and molded wastebasket are installed in all four
guest rooms. Source scale is retained: rack 0.5588 × 0.5080 × 0.3530 m;
basket 0.2984 × 0.3112 × 0.2096 m. Floor roots are at Y=0.008 m.
Racks sit beside the front wall; baskets sit beside the desks. Existing actor
spawns, door swings and the established capsule routes are unchanged.

The original 146 motel instance IDs remain stable. Eight new owners are
appended, one rack and basket per room. Each receives the same cached room and
sun shadows as existing furniture. Rendering/removal follows its owner.

## Physics and replication

Collision uses the actual LOD0 triangles, transformed once into body-local
coordinates by `tools/import_motel_utility.c`. The generated header is compiled
into the headless simulation: training/server operation does not load art files
or run an importer. The rack's openings and basket's cavity stay open.

These props are static furniture in this pass. They have no pickup, folding,
tipping or authored breakage behavior. Both use the existing WOOD gameplay
material category, also used by several original motel furnishings. This is a
placeholder for the basket's impact/penetration/acoustic response; its visible
material is plastic. A dedicated polymer response remains future work.

Ordinary map objects carry their bounds and transforms. The receiver validates
the complete known recipe before restoring detailed collision. Original
147-object motel maps are still accepted, with no added props. Current maps
contain 155 objects. No protocol field or layout-token format was added.

## Materials

Original 512 px basecolor, normal and roughness/metallic maps use UV0. Separate
AO uses the authored UV1 buffer and its glTF strength. The shader now supports
this independent UV selection; existing materials default back to UV0 after
each draw. AO only affects indirect lighting. LOD0 is active; supplied LOD1
files are retained for future projected-size selection, not automatically used.

## Validation and review

- All 44 archive payload hashes verified before import.
- Linux and native Windows motel tests pass: walking routes, functional doors,
  rays through rack openings and basket top, rail/base/side hits, matching
  replica collision, old-map compatibility, invalid-recipe rejection and reset.
- Linux and native Windows lighting tests pass, including a GPU test with
  different UV0/UV1 coordinates that measures independent AO selection and
  restoration to UV0. Existing room-shadow and PBR tests also pass.
- Native environment test confirms second UV buffers/material bindings,
  metre-scale geometry, individual removal and unchanged authoritative state.
- Protocol regression and normal `./swat play --mission motel` capture pass.

`utility-rack.png`, `utility-basket.png` and `utility-room.png` are unedited
native Windows renderer captures, with contact AO disabled. The basket's near
rim shows some faceting at this close distance; room-scale placement is suitable
for the current game-fidelity direction. Further prop dressing is ongoing.

## Provenance and rebuild

Original CC0 package:
https://drive.google.com/file/d/1XC7guxAaVt4ovPMBIhhpRWn3G3S0kkYF/view

Original import notes, manifest, material/scale metadata, references and license
are retained alongside this directory. The editable Blender file is in
`../source/`. Runtime GLBs are in `assets/environment/motel_utility_v1/`.
The package's duplicate loose textures, previews and download archive are not
retained locally; images are embedded in the GLBs and the full package is backed
up on Drive. The supplied manifest describes that original archive layout.

To regenerate only the collision header, from `ocean/swat`:

```bash
cc -O2 tools/import_motel_utility.c -lm -o /tmp/import_motel_utility
/tmp/import_motel_utility > motel_utility_data.h
```

No Python is required by this import or the game.
