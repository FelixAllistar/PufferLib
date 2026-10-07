# Daylight and contact shading

The free CC0 Poly Haven sky in `SOURCE.json` drives both the visible sky and
outdoor surface illumination/reflections. The original HDR is preserved. The
player loads `daylight.bin`, a checked-in RGB32F atlas baked in C with cosine
diffuse convolution, six GGX roughness levels and a split-sum BRDF lookup. No
convolution runs during play. The detected solar disc supplies the shadowed sun
and is removed from the ambient convolution to avoid counting it twice.

`make -C ocean/swat lighting-bake` rebuilds the atlas. This authoring operation
does not change physics or training configuration. The HDR decoder is stb_image
from Raylib 5.5, retained under its original dual MIT/public-domain license in
`ocean/swat/vendor/stb_image.h`; only the offline tool includes it.

Half-resolution camera depth supplies subtle, 28 cm contact occlusion on nearby
world geometry. Only indirect illumination is darkened. Geometry removal changes
the result immediately, with no physics rays, history buffer or baked wall shadow.
Depth matching prevents it from being projected onto unrelated foreground models.
First-person weapons and small secondary camera feeds omit this contact pass.
The original world depth buffer, collision and viewmodel projection are untouched.

Imported occlusion textures use their red channel and glTF strength on indirect
light only. Base color is gamma-decoded; normal, roughness, metalness and AO data
are linear. Room 101's material metadata restores AO strength along with the
other scalar factors. Existing indoor fill remains a room approximation: this
pass does not claim local reflection probes or full global illumination.

For renderer comparisons, `SWAT_IBL=0` selects the previous analytic sky and
`SWAT_CONTACT_SHADOWS=1` enables the experimental contact pass. Both are forwarded by the
normal WSL-to-Windows launcher. Missing or invalid atlas data falls back to the
analytic sky. The main game starts with HDR lighting enabled and contact shading
disabled: indoor firing measurements on the GTX 1060 still showed substantial
extra frame time with that geometry pass, despite culling and texture-free depth
draws. Existing room-bound contact shading remains enabled.
