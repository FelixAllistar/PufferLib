# BC_SEDAN01 · parked compact sedan · r01 candidate

Original unbranded four-door compact sedan, intended as static environmental art for a motel parking area or roadside scene. The restrained, older compact design has a shaped hood/deck/roof, four real wheel-arch openings, opaque glazing with coherent A/B/C pillars, closed doors, mirrors, lenses, blank registration panels, bumper rub strips and pressed-steel wheel covers. No existing building, canopy, route or facade was modified.

## Files and placement

- Runtime: `runtime/BC_SEDAN01_parked.glb`
- Editable source: `source/BC_SEDAN01_editable.blend`; all five PNGs are packed
- Metres; common root and all runtime nodes are at the ground-centered origin, with identity transforms
- Blender axes: X left/right, Y length, Z up; vehicle front is **−Y**
- glTF axes: X left/right, Y up, Z length; vehicle front is **+Z**. The exporter already applies the axis conversion once.
- Full length × width including mirrors × height: **4.362 × 1.960 × 1.454 m**
- Coachwork width: 1.750 m. The wider footprint includes both mirrors.
- Wheelbase: 2.560 m. Tires: 0.315 m radius, 0.200 m width.
- Wheel centers, glTF X/Y/Z metres: front left (+0.765, 0.315, +1.280), front right (−0.765, 0.315, +1.280), rear left (+0.765, 0.315, −1.280), rear right (−0.765, 0.315, −1.280).

## Inventory

**13,124 triangles; 6 mesh nodes plus one root; 12 material primitives; 4 opaque, single-sided metallic–roughness PBR materials; 5 embedded PNG images.** The source retains 151 named closed solids with non-destructive triangulation modifiers. The runtime groups these into coachwork/trim, opaque windows, and four wheels. Wheel nodes use the shared ground origin, not operative wheel-center pivots; the centers above are descriptive placement metadata.

The proposed 8–12k triangle range was advisory. The approximately 13k final budget preserves rounded tire/arch and body silhouettes, coherent trim and closed editable source geometry. These counts are offline inventory, not a certified draw-call or frame-time result.

## Rebuild

Requires Blender 4.3.2 and Python 3 with NumPy and Pillow. No external model, add-on, network access or downloaded texture is required.

1. `python source/make_textures.py`
2. `blender -b --python source/build_sedan.py`
3. `blender -b --python source/render_export.py`

To rebuild in a fresh folder, set `SEDAN_OUT=/absolute/fresh/output` for all three commands. The texture recipe generates the five original maps in that folder. The geometry recipe consumes those maps and saves the runtime and packed source there. If maps do not yet exist in the output folder, the geometry recipe reads the adjacent supplied maps; regenerate them first when testing the full recipe.

To export from the packed editable source alone:

`blender -b source/BC_SEDAN01_editable.blend --python source/export_packed.py -- --output /absolute/output.glb`

The geometry, UVs, normals, material content, textures and placement are the reproduction contract. A `.blend` file's save metadata can differ between runs. `qa/independent/` contains the independent reopen/rebuild comparison and scope.

## Scope and limits

This is a static exterior with opaque sealed glazing. There is no interior, skeleton, rig, animation, driving behavior, steering, collision mesh, suspension, damage/destruction, material variants, LOD chain or engine implementation. The lamps are opaque colored lens surfaces and do not emit light. Registration panels are deliberately blank, with no real plate identity. The source's closed glass/frame overlay treatment is appropriate to this exterior-only scope; it does not represent transparent vehicle glazing or interior construction.

The engine team owns optional placement, colliders, routes, damage hooks, batching/LOD decisions and target-platform performance acceptance. Real manufacturing accuracy, roadworthiness or safety compliance is not represented. No live-engine coverage or universal absence of cars is claimed: the bounded duplicate search is documented in `qa/duplicate_check.json`.

## Provenance and review

All asset geometry, materials and deterministic bitmap artwork are original to this candidate. No downloaded car model, manufacturer logo, vehicle blueprint, external image, inherited tire mesh or reused material map is included. No new public-domain dedication or general-purpose third-party license is granted. See `PROVENANCE.json` for exact input identities and generation details.

`previews/` contains fresh-runtime-import front, rear, side, eye-level, three-quarter, top, underside and wheel-contact renders. The neutral plane, lights and camera are review-only and are never exported. `qa/render_binding.json` binds each preview to the runtime hash. `qa/` and the independent report distinguish verified offline checks from untested engine integration.

Known minor visual caveats in this accepted revision: a faint/intermittent rear-deck shut line in oblique views low-poly highlights on the lower front valance, and uneven wheel-cover slot visibility in close-up. See `docs/KNOWN_LIMITATIONS.md`.
