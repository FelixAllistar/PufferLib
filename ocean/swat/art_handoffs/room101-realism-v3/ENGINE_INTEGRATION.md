# Room 101 v3 engine integration

The 12 runtime GLBs are installed in `assets/environment/motel_room101_v3`.
All hashes in the supplied `RUNTIME_SHA256SUMS` were verified before installation.
The complete runtime material images and author metadata are retained here;
runtime GLBs are stored once in the asset directory. High-resolution Blender
sources remain in the author's verified [Drive backup](https://drive.google.com/drive/folders/14ztEpKf1FPSHhkwj2dD5bOf-c1gt1Lt2).

V3 overrides only the ten specified Room 101 instances and two owned trim
overlays. The v2 facade, main door, walkway and sconce remain. The new plaque
contains the numerals, so the old numeral overlay is omitted. Inward-facing wall
variants are instance-specific; other rooms retain their existing art.

The loader preserves node transforms, original instance scale, normal-map
strengths and scalar glTF factors. Carpet AO uses red at strength 0.30, with
normal strength 0.18. Panes remain opaque and the same world objects own all
children, shadows and damage. No collision, visibility, navigation or simulation
data was changed by this integration.

V3 loads as an atomic set over v2. An incomplete/invalid v3 set is released and
the full v2 presentation remains. `SWAT_MOTEL_ROOM101=2 ./swat play --mission
motel` selects v2 explicitly; `0` selects the original motel bank.

Graphics validation checks the ten replacement envelopes against the original
models, embedded AO and normal strengths, matched entrance/interior captures,
door motion and removal, world immutability, cutaway path and resource lifecycle.
The captured blanket still reads like a cushion; the bed frame and original
furniture remain simplified. Those are outstanding art refinements, not claims
of finished realism.

The accompanying renderer change gives every room its own 768-pixel shadow tile
in a 4-by-2 depth atlas. Static room geometry is cached independently. Moving
people are composited onto copied static depths every four ticks; camera motion
never evicts another room's shadow. Doors and destruction invalidate nearby tiles
immediately. This remains bounded room lighting with downward light projections,
not omnidirectional point-light shadows or global illumination.
