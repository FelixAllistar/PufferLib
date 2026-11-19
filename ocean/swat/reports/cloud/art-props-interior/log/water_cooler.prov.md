# water_cooler — provenance
- Reference: "Watercooler Wikinews.jpg", photo by Jason Pratt (FishSpeaker), CC BY 1.0 — https://commons.wikimedia.org/wiki/File:Watercooler_Wikinews.jpg (also Flickr https://www.flickr.com/photos/84108876@N00/2327979, CC BY 2.0). Committed downscaled copy: refs/water_cooler.jpg (413x1106, 42 KB), credit required.
- Note: the reference is a later (1990s-2000s) bottled cooler; the model keeps its form (tall cabinet, open front recess with push faucets, inverted 5-gallon bottle) and adds an era side paper-cup dispenser.
- Textures: none downloaded. One 256 px enamel map baked from seeded numpy noise in the script; everything else is PBR factors.
- Asset: assets/environment/motel_props_int_v2/water_cooler.glb — 2,108 triangles, 7 materials, 137,952 bytes; bounds x -0.164..0.238, y 0..1.393, z -0.004..0.324 (origin floor, back-edge centre; front +Z).
- Build: blender -b --factory-startup --python-exit-code 1 -P ocean/swat/tools/blender/props_int_v2_water_cooler.py -- ocean/swat/assets/environment/motel_props_int_v2 [--render PNG]
- Iterations: 10 (Blender).
