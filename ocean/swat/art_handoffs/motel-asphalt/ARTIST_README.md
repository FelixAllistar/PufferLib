# BriarCourt parking asphalt v1 — source/art candidate

## Selected material
Poly Haven **Clean Asphalt**, Dimitrios Savva, CC0.
https://polyhaven.com/a/clean_asphalt

Use the three unchanged 1024×1024, 16-bit PNGs under runtime/:
- clean_asphalt_diff_1k.png → base color, sRGB decode
- clean_asphalt_nor_gl_1k.png → OpenGL tangent normal, linear/non-color, strength 1 initially
- clean_asphalt_rough_1k.png → roughness, linear/non-color

Metallic = 0. No AO multiply, displacement, parallax, height, or painted lighting. Do not treat normal/roughness as sRGB even though a generic image inspector may label the PNG container that way. No channel packing or local image editing was performed. PNG decoder support for 16-bit inputs remains an engine integration check; if conversion is required, retain these originals and use an explicitly approved deterministic conversion pipeline.

## Scale and binding handoff
One full tile = **2.1 × 2.1 meters**, from official source dimensions (not an invented 2 m repeat). This gives ~487.6 texels/m, 2.05 mm/texel, and ~22.86 repeats across a 48 m court. Source micrograin is resolution-limited at very close range; do not increase the physical grain size merely to hide 1K sampling. Use repeat wrapping and engine-supported mipmapping/anisotropy; engine implementation is outside this package.

Parent-supplied geometry facts: owner 0 is the untextured fallback cube centered (0, −0.58, 0), half-extents (24, 0.5, 24), top Y = −0.08, no authored UVs. Intended scope is only the motel parking owner 0 visual material. Physics concrete tag, geometry, sidewalks, floors, kerbs and ramp remain outside scope.

Suggested mathematically right-handed upward-plane convention: U = world X / 2.1, V = −world Z / 2.1; tangent +X, bitangent −Z, normal +Y. Engine may use another consistent convention, but must validate normal orientation against its actual shader basis. Do not flip the green channel just because a texture is described as OpenGL. UV sign and tangent handedness have to agree. QA scene uses Blender XY ground with its native tangent convention; it is a material test, not proof of engine binding.

## Optional low-frequency variation (OFF by default)
A bounded procedural recipe, not supplied engine code or an implemented shader feature:
1. Evaluate centered smooth value noise over world XZ at 12 m and 27 m wavelengths, independent fixed seeds.
2. Mix 0.7 and 0.3; clamp result n to [−1, 1]. Avoid resetting noise each material tile.
3. Apply base-color multiplier 1 + 0.025 n in linear light (±2.5% maximum).
4. Apply roughness addition 0.035 n, clamped to [0, 1]. Optional roughness-only mode avoids color adjustment.
5. Keep normal, geometry and displacement unchanged.

This adds broad, subtle variation without baked directional shading. It does **not** fix source seams or erase repeated identifiable marks. A 2D value-noise implementation commonly needs four lattice values per octave plus interpolation; two octaves add eight evaluations. A preexisting noise texture implementation instead usually adds two texture samples. Actual performance, shader availability and resulting appearance are unverified. Evaluate only if separately enabled by the engine owner; tune down or disable if it looks cloudy. The main QA previews contain no such variation.

## Reproduce / audit
- python scripts/verify_channels.py — byte identity, hashes, seam step diagnostics
- blender -b -t 6 --python scripts/render_qa.py — fixed-light independent material previews
- python scripts/download_source.py — re-fetch exact selected URLs and check retained MD5/SHA-256

All paths are relative to each script. Source files and manifests are preserved. The 1K files were selected directly from the official API, not derived locally. QA scene geometry is a separate test plane only. No BriarCourt engine or game geometry was touched. No external upload was made.

See qa/QA_REPORT.md for the actual visual gate and outstanding engine checks.
