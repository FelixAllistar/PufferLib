# Rigid weapon integration

The local player loads the privately supplied `rifle7_rigid_textured.glb` for
carbine profile 0, in officer and world views. Other profiles and absent assets
use procedural geometry. Live animated officers, camera feeds, shadows and
first-person arms borrow the revised rigid body at the sampled `Prop_Rifle`
transform. The original character rifle body is suppressed; the original
magazine meshes keep their authored reload motion and use the revised maps.

Install the approved export at
`build/swat/assets/weapons/rifle7_rigid_textured.glb`. The Windows builder copies
that local file to its executable's `assets/weapons` directory; CMake copies it
when available at configuration time. Alternatively set `SWAT_WEAPON_ASSETS` to
a directory containing that filename. Explicit directories never mix in default
files. `SWAT_WEAPON_ART=0 ./swat` selects the procedural comparison. WSL forwards
both overrides to the native player, translating the directory path.

The source has no public redistribution authorization. Its binary, textures,
neutral views and editable sources stay in private Drive/local ignored build
storage. The public repository contains only the consumer and measured interface.
The installed R2 source SHA-256 is
`09d659865fb4d75aaed64b91f973e98ae178d12ffd9df42b8fc401cac25fce98`.
The frozen original is
`daac57ca6bf40c9157808f88a1d1184c11f19ea4bada63cc54ca260b24392b48`.

Install the extracted, private R2 handoff with:

```
python3 ocean/swat/tools/install_weapon_revision.py EXTRACTED_R2_FIXTURE
```

The installer checks pinned payload hashes, the rigid frame, exact original
vertex prefixes, unchanged bounds and magazine geometry before replacing the
canonical local asset. It keeps `rifle7_original_textured.glb` for comparison.
`--material-only` installs the separately verified finish-only variant.

R2 rounds the rear sight's outer contour from eight to 32 angular segments,
replacing 64 outer-ring triangles with 1,024. The largest cosmetic contour move
is 0.442354 mm. All 10,209 other body triangles and the 780 magazine triangles
are unchanged. The inner aperture remains octagonal; sight centers, bore,
stock, measured hand contacts and overall bounds are unchanged. This improves
the outer silhouette without changing the physical sight opening.

## Authority and source dimensions

The two static identity meshes are the body and seated magazine, in metres,
stock-pad origin, +X forward, +Y up, +Z right. No scaling or deformation is
applied. `rifle_geometry.h` supplies the same local points to the simulation's
pose and the renderer's rigid transform:

| Binding | X | Y | Z |
| --- | ---: | ---: | ---: |
| Muzzle | .89999995 | .04362635 | 0 |
| Rear sight | .29982166 | .12474874 | 0 |
| Front sight | .65497363 | .12472590 | 0 |
| Right-hand candidate | .26177320 | -.07099672 | .01466539 |
| Left-hand candidate | .55807343 | .01222156 | 0 |

Stock-to-muzzle length is .9010567 m; the weapon definition's `.90 m` barrel
field represents forward reach. Rear sight-to-bore height is .08112239 m.
ADS aligns the measured rear aperture and front post with the aim ray;
high/low ready rotates the entire assembly. First-person eye relief is
adjustable from 80 to 220 mm, with a 120 mm default. Full-influence source
hands retain their authored grip, with a measured engine support-thumb wrap
that releases during reload. Remaining palm/finger contact needs art iteration.
The existing .035 m eye-to-muzzle sweep and firing origin now use these points
even without art. This changes carbine cover clearance and policy behavior.
Network/replay version **8** rejects version 7 peers and recordings; upgrade
clients/server together. Source RL observation/action shapes remain unchanged.

## Materials and reload

R2 retains the original 4096-square UV layout and normal-map image, with revised
base color and packed metallic/roughness maps: restrained bright wear, matte
coated metal and separate polymer/rubber finishes. The loader reads the authored
glTF `normalTexture.scale` of 0.65, and the shader scales tangent-space XY before
normalizing. Unrelated surfaces retain their existing strength of 1. Mipmaps and trilinear filtering reduce
distance shimmer. The mesh shader reads roughness from G and metalness from B,
converts only base color to linear light, and adds GGX direct highlights plus an
approximate hemisphere reflection under the existing sun/room lights and depth
shadows. This is an initial material pass, without scene environment probes or
full image-based lighting. The loader releases shared texture IDs once.

The standalone magazine follows authoritative `magazine_seated`: it disappears
at reload removal and returns at insertion. Animated officers and arms retain
the original held/removed/fresh magazine meshes and reload motion. No ammo
transfer is inferred from rendering.
Sight profile tuning remains mechanical; the export has its authored irons,
without red-dot/optic attachment meshes. Animated character shadows use the revised rigid body at the cached pose.
Procedural fallback actors keep their existing silhouette.

The optional source sidearm is untextured and imports at about 2.715 m. Its
physical-size reference and grip fit need authoring calibration before it can
replace the sidearm placeholder. The immutable F fixture is unchanged.

## Checks

Headless foundation tests cover measured ADS/bore alignment, orthonormal ready
transforms, muzzle length, clearance, exact replay and every reload interruption.
The explicit GPU test (requires the private export) checks 4K maps/tangents,
model-to-authority transform agreement, visible magazine removal, normal-map
response and authored strength, immutable arsenal, opt-out/missing-asset/equipment fallback and cleanup:

```
make -C ocean/swat weapon-art-test-build
GALLIUM_DRIVER=d3d12 ./build/swat/test_weapon_art build/swat/assets/weapons build/swat
```

On Windows run `test_weapon_art.exe assets/weapons .` from `build/swat/windows`.

Local GTX 1060 3GB native check, 600 measured frames after 30 warmup frames,
generated seed 42, 1,154 world objects, ten actors, audio, replay writes and a
compact live sniper feed: mean 8.925 ms, median 7.489 ms, p95 14.690 ms,
maximum 17.088 ms. The rigid export was loaded. This fixed-scene measurement
does not establish a worst-case gameplay budget. The full UI suite passed two
consecutive unchanged runs after one intermittent charge-mount automation
failure; no input assertions were relaxed.

R2 validation: all 21 headless tests pass. The explicit GPU suites pass
with the original and revised weapon, including all seven motion banks,
source grip release, reload visibility, camera pose restoration and physical
ADS alignment through eye-relief, yaw, lean, recoil and pitch limits. Matched
engine hip/ADS/world captures remain in ignored local storage. Native GTX 1060
checks pass for both the revised animated character and standalone rifle, with
normal strength 0.65 confirmed in the runtime logs.
