# Original Ch15 character textures

Nine original 2048×2048 PNGs, copied byte-for-byte for private engine preview.
No images were resized, re-encoded, inverted, packed, baked or generated.
The current Ready N GLB, historical F and rigid rifle are unchanged.

## Assignment

Use texture_bindings.json for exact image hashes, dimensions and assignments.
For Ready GLB SHA256 3a20375ec72f106925ef96718da0931e172f21908c6720cff4d792c173c0cc81:

- Body node 71, mesh 1, primitive 0, material 1 (Ch15_body.001): Ch15_1001 maps
- Body node 71, mesh 1, primitive 1, material 2 (Ch15_body1.001): Ch15_1002 maps
- Both use TEXCOORD_0, derived from source UV layer map1

These indices apply to that exact GLB. Resolve names and compatibility again
for a different asset. Positions, topology, UVs, face-material assignments and
both body primitives' position/normal/UV/index bytes match the surviving
textured source. CHARACTER_MATERIAL_INVENTORY.json records the evidence.
The reconstructed sleeve, magazines and rifle are outside this body-texture proof.

## Observed source material semantics

Diffuse and 1002 Emissive images use sRGB. Normal, Specular and Glossiness use
Non-Color data. Preserve those distinctions; do not apply an sRGB transfer to
normal or scalar data maps. Both material graphs use metallic 0 and roughness 0.78.
They leave Glossiness unlinked. No native metallic, roughness or AO map was found;
a gloss-to-roughness conversion is not established by this handoff.

Both Normal Map nodes use TANGENT space and strength 1.0. Their UV selector is
empty, using the active/render map1 layer. Source image projection is FLAT,
extension REPEAT. This proves the surviving Blender graph configuration; an
independent original-author declaration of green-channel handedness is absent.
Do not silently invert a channel. The neutral Ready GLB has no TANGENT accessor:
the consumer needs a consistent tangent frame before applying these normal maps.

RGB Specular Color directly feeds the scalar Principled Specular IOR Level
socket in the surviving graph. It is not a metallic map or verified RGB F0 map.
The historical GLB instead references the PNG through KHR_materials_specular's
alpha-only specularTexture. These RGB PNGs have no alpha, so its effective alpha
is 1 and that path loses their spatial RGB signal. Treat the old GLB as storage/
geometry evidence, not a faithful specular-response oracle. No map conversion
or shader calibration is performed here. The 1002 Emissive map feeds Emission
Color at strength 1; no physical emission intensity is established.

## Provenance and verification

Local Oct1 records identify Adobe Mixamo Ch15_nonPBR. All nine files match the
packed bytes in the surviving original textured Blender asset. The original
character FBX, exact license receipt and original license text are absent.
Applicable original Adobe/Mixamo terms remain in effect; this package adds no
license grant or public redistribution authorization. Keep the files private.

The two HISTORICAL_* documents preserve that source record. Their old animation,
proxy-rifle and helper-socket descriptions are historical, not current calibration.
See SOURCE_PROVENANCE.json and material_semantics.json for sources and limits.
Every file is listed in manifest.json. All PNGs were opened for integrity checks,
all hashes match the originals, and the ZIP CRC and member hashes were verified.
No engine rendering or final material-appearance acceptance is claimed.
