# Phase A ground material integration

The original 512px zoning mask and three new 1K Poly Haven dirt scans are installed
under `assets/environment/motel_zoning`. Six supplied 1K source hashes were
verified before import. The existing gravel diffuse/normal PNG bytes remain in
the existing ground GLB; no second gravel set, reference GLB, 2K alternatives or
new terrain geometry is loaded. `INSTALLED_FILES.json` hashes the installed
source bytes and unchanged render/collision GLBs. Source licenses and scan links
are retained. The added original runtime data is about 12.6 MB.

The 54 support owners and physical mesh data remain unchanged. The frozen mapping
selects twelve flat soil owners. The shader additionally gates on upward top
normals; sides, bottoms, road, driveway, old court, aprons and shoulder/bank keep
their own materials. The mask uses world XZ, PNG row zero at -Z, clamp, bilinear
LOD0 and one R8 level. No mip chain can leak dirt into exclusions. Outside the
finite mask bounds its weight is zero. Dirt retains 2 m repeat and the original
world-zero phase, derived from exported gravel UV with the V offset preserved.

Color blends after exact sRGB decoding and the original .65 linear gravel factor;
roughness interpolates linearly, metalness is zero. Both normals use .65 XY scale,
are normalized before interpolation and renormalized in the same existing signed
UV-derivative tangent basis. No AO, height, displacement or painted illumination
is added. Eligible gravel tops now use the exact transfer rather than the old
2.2 approximation/quantized material tint; this can slightly change their zero-
weight baseline. The protected road/driveway do not enter that branch.

Four existing, otherwise inactive sampler slots carry the mask and dirt channels
for ground draws. There are still thirteen fragment samplers, within the OpenGL
3.3 minimum of sixteen units. Local per-draw material-map copies leave GLB texture
ownership untouched. Every ordinary material/frame/surface binding clears blend
state. Missing/incomplete zoning data keeps the original gravel rendering;
partial allocations are released. Location switches release all four standalone
additions; the ordinary player needs no special command or config.

Read-only source contract check (Node, no Python):

    node ocean/swat/tools/validate_motel_zoning.cjs

Native graphics check uses the existing `test_environment_art` target with the
`zoning` selector and a capture directory. It compares the production shader to
independently calculated diffuse/roughness/normal inputs, using asymmetric
repeating float textures and the actual installed R8 mask. All 39 RGB channel
comparisons match exactly on the native GTX 1060, including original dirt phase,
mask row orientation, zero/outside weights and bottom-face exclusion. The aerial
mask-on/control comparison changes 225,413 soil pixels while 27,424 protected
road/drive pixels remain identical at a minified view. Another 75,210 bilinear
world probes stay zero throughout the protected drive/road. Source material maps
and authority memory remain byte-identical. OpenGL confirms all four additions
are deleted on location change and they reload successfully.

Existing physical ground tests pass: 740 rays, closed thickness, court/drive/
road/end-grade controller crossings, squad crossing, finite boundaries and wire
reconstruction. Linux and native player builds pass. Captures/logs live under
`build/swat/review/ground-zoning`, not in a duplicate art release. Eye and aerial
views are native engine renders; the aerial magenta margin marks empty coverage.
This is a material foundation: background composition, conspicuous repeat and
far east-wall patching visible in both mask/control captures remain polish work.

The original artist README is retained verbatim and describes its larger source
packet, not the minimal files installed here. Full editable zoning/Blender and
original scan backups are in the original Phase A Drive folder linked in the
receipt. A compact generator/config/control-point handoff has been requested
for local source preservation. No original Python authoring code was executed.
