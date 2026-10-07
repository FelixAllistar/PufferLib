# Briar Court • Room 101 v3

A bounded entrance-and-interior art candidate built on the preserved v2 doorway. Twelve GLBs revise ten existing assembly instances and two decorative overlays. The ten instances represent nine existing designs because the two shared end walls are instance-specific variants of one design. This is not a new location kit and must not increase the original distinct-design count.

## What changed

- Painted 101 typography using a licensed font instead of physical box-grid or shaped glyphs; mounting hardware remains physical
- Window track/meeting profiles and latch; continuous short pleated curtains behind separately named opaque panes
- Finer folded-metal AC louvers, shallow intake recess, grille frame and fasteners within the old cabinet envelope
- Room 101 carpet and inside-facing wall/partition/ceiling finishes; shared-wall outside faces retain their original material/UV
- Photographic linen surface detail, a softer pillow and three folded blanket layers; existing mattress base and room furniture remain
- Precisely trimmed v2 bead joints and properly seated decorative casing/caulk. No main facade or door mesh rewrite

The window remains opaque. Moving the curtains behind it intentionally removes their former physically incorrect visibility from outside; they are visible in the matched interior-window view. Pane/frame/curtain source nodes are separate for a later engine-coordinated visibility/destruction change, but currently share the original window authority owner.

## Review images

qa/baseline_v2_* and qa/candidate_v3_* are matched source renders. CAMERA_MATCH.json records source Z-up and engine Y-up eye/target/up, FOV, resolution and door pose for doorway, interior bed and interior window views. DETAIL_CAMERAS.json adds high-sample trim/fabric views. The latter are 384-sample maximum adaptive renders, not denoised or post-filtered images.

Lighting is held at the prior Blender scene's world/room/courtyard settings for each pair. These images do not establish parity with the engine's new HDR/reflection/contact-shading work. Actual in-game comparison and adoption remain engine-owned. CURRENT_ENGINE_BASELINE.json and qa/engine_default_v2 preserve the subsequently supplied default HDR v2 captures. ENGINE_CAMERA_MATCH.json records their exact 960×800 entrance/interior cameras; *_engine_*_source_lighting.png are Blender counterparts with matching cameras but different source lighting, not engine captures or HDR parity.

## Honest limits

The mattress side is still rectilinear, and the thick folded blanket retains a cushion-like simplified silhouette. The existing institutional bed frame, desk, folding chair, TV and bathroom fixtures remain stylized context. Fabrics use source-backed surface detail, but their shape is manually authored rather than cloth simulation. This is a small next handoff, not an approved photorealistic room or Ready or Not-equivalent result.

Two inherited original facade jamb/head overlaps and its modular construction grooves remain. Revised beads have no exposed corner overlap; seated casing still has buried underside/plinth contact at floor level. Longer changing edge artifacts can also involve renderer/shadow behavior. The inherited +0.4mm threshold offset is unchanged.

## Runtime and source

Start with RUNTIME_HANDOFF.md and RUNTIME_INSTANCE_BINDINGS.json before integration. Do not simply import every source object or regenerate collision from these visual revisions. V2 remains a dependency for the unchanged facade, main door, walkway and sconce. The v3 plaque replaces the v2 plaque and its numeral overlay; the two new v3 overlays replace their v2 counterparts.

- assets/: 12 exported GLBs, with original root datums and owned child transforms
- room101_v3_source.blend: editable packed scene, authored door restored to 100 degrees
- source/room101_v2_pinned.blend: immutable input checkpoint
- source_materials/: untouched higher-resolution photographic PNG inputs and source provenance
- textures/: 1K-maximum runtime derivatives and painted plaque artwork
- fonts/: unmodified DejaVu font and its redistribution notice
- prepare_maps.py / build_candidate.py / render_detail_pairs.py: reconstruction and capture recipes
- qa/: validations and review images
- CATALOG_ENTRIES.json: revision records for catalog reconciliation, not new-design claims

Python preparation requires NumPy, Pillow and ImageMagick convert. Blender 4.3.2 was used. Run prepare_maps.py, then build_candidate.py in Blender. --baseline produces original wide views; --export-only skips candidate renders. render_detail_pairs.py produces the matched closer views. No runtime code or live game repository is changed by these recipes.

## Material provenance and scale

New sources are free CC0 photographic surfaces:

- Poly Haven Rough Linen, colormass photography / Rico Cilliers processing: https://polyhaven.com/a/rough_linen ; https://polyhaven.com/license
- ambientCG Carpet016, surface photogrammetry: https://ambientcg.com/view?id=Carpet016 ; https://docs.ambientcg.com/license/

Linen source is 2048×2052. Its published height is rounded to 0.3m; runtime uses 1022×1024 and approximately 0.2994×0.3m repetition. This is a chosen interpretation of the published approximate scale, not newly measured cloth. Carpet source is 2048², with a published approximate 1.7×1.7m repeat; runtime is 1024². Normals are +Y/OpenGL and linear; roughness is linear. Original 16-bit data is retained, resampled normal vectors are renormalized, and diffuse resampling is done in linear light.

Colors and source variation are deliberately restrained through documented artistic tint/contrast settings. No directional illumination or AO is added to basecolor. We do not certify the original photographic diffuse as calibrated albedo. Carpet AO is preserved separately and exported as linear ORM red with glTF occlusion strength 0.30. It is never multiplied into basecolor. Other candidate materials use unbound/white AO. AO_CHANNELS.json records the exact packing checks.

Existing White Plaster 02, Brushed Concrete and door-paint provenance remain in source/V2_* metadata and the preserved v2 backup. Their higher-resolution originals remain in the verified v2 Drive source archive, SHA256 7112484674d4846c81517f4aa1fae6c8b097a18ad099b2d84200016bdf83450d. This package includes the exact packed v2 input needed for rebuilding; it does not duplicate all those earlier raw source files.

Painted typography uses DejaVu Sans Condensed Bold, with the Bitstream Vera notice in fonts/. No commercial logo or manufacturer identity is applied to the fictional assets. Manufacturer references guide common construction details only; the AC is not a model-accurate GE product replica.
