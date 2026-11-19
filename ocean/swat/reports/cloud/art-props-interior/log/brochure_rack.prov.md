# brochure_rack — provenance
- Reference: "SM Brochure Rack" by vastateparksstaff (Virginia State Parks), Flickr, CC BY 2.0 — https://www.flickr.com/photos/37922399@N05/50236900808 (found via Openverse). Committed downscaled copy: refs/brochure_rack.jpg (600x449, 54 KB), credit required.
- Textures: Poly Haven "oak_veneer_01" diffuse, CC0 — https://polyhaven.com/a/oak_veneer_01 (1k JPEG downscaled to 512 px, tinted cherry in the material). Brochure cover atlas (512 px) baked from seeded numpy noise: made-up generic graphics, no real text, brands or logos.
- Asset: assets/environment/motel_props_int_v2/brochure_rack.glb — 354 triangles, 3 materials, 462,884 bytes; bounds x ±0.25, y 0..0.812, z -0.004..0.104.
- Mount: WALL. Origin on the wall plane at the rack's bottom centre (local y=0 is the rack bottom); place the mount 0.75 m above the floor.
- Build: blender -b --factory-startup --python-exit-code 1 -P ocean/swat/tools/blender/props_int_v2_brochure_rack.py -- ocean/swat/assets/environment/motel_props_int_v2 [--render PNG]
- Iterations: 7 (Blender).
