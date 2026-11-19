# waiting_chairs — provenance
- Reference: untitled beam-seating photo by bfick, Flickr, CC BY 2.0 — https://www.flickr.com/photos/48948205@N00/3214261837 (found via Openverse). Committed downscaled copy: refs/waiting_chairs.jpg (600x456, 58 KB), credit required. Material cue (worn vinyl tub chairs): "Wait here!" by zeitfaenger.at, Flickr, CC BY 2.0 — https://www.flickr.com/photos/46652337@N06/15269524910 (not committed).
- Textures: Poly Haven "fabric_leather_02" diffuse, CC0 — https://polyhaven.com/a/fabric_leather_02 (1k JPEG -> 256 px, desaturated and graded to faded avocado vinyl, re-embedded as PNG).
- Asset: assets/environment/motel_props_int_v2/waiting_chairs.glb — 5,372 triangles, 4 materials, 273,248 bytes; bounds x ±0.545, y 0..0.832, z -0.004..0.574 (origin floor, back-edge centre; front +Z). Seat height 0.44 m.
- Build: blender -b --factory-startup --python-exit-code 1 -P ocean/swat/tools/blender/props_int_v2_waiting_chairs.py -- ocean/swat/assets/environment/motel_props_int_v2 [--render PNG]
- Iterations: 7 (Blender).
