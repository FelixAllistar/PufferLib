# Rigid weapon integration

The local player loads the privately supplied `rifle7_rigid_textured.glb` for
carbine profile 0, in officer and world views. Other profiles and absent assets
use procedural geometry. This is a rigid weapon consumer; the full character
skinning/animation adapter and anatomical hand fit remain unfinished.

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
The installed source SHA-256 is
`daac57ca6bf40c9157808f88a1d1184c11f19ea4bada63cc54ca260b24392b48`.

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
ADS aligns the rear sight with the aim ray; high/low ready rotates the entire
assembly. Hand points are measured placement candidates, with placeholder
hands; they do not establish finger wrapping, stock fit or full-body animation.
The existing .035 m eye-to-muzzle sweep and firing origin now use these points
even without art. This changes carbine cover clearance and policy behavior.
Network/replay version **8** rejects version 7 peers and recordings; upgrade
clients/server together. Source RL observation/action shapes remain unchanged.

## Materials and reload

The original embedded 4096-square base color, tangent-space normal and packed
metallic/roughness maps remain unchanged. Mipmaps and trilinear filtering reduce
distance shimmer. The mesh shader reads roughness from G and metalness from B,
converts only base color to linear light, and adds GGX direct highlights plus an
approximate hemisphere reflection under the existing sun/room lights and depth
shadows. This is an initial material pass, without scene environment probes or
full image-based lighting. The loader releases shared texture IDs once.

The magazine mesh follows authoritative `magazine_seated`: it disappears at
reload removal and returns at insertion. No ammo transfer is inferred from
rendering, and cosmetic held/dropped magazine animation is still pending.
Sight profile tuning remains mechanical; the export has its authored irons,
without red-dot/optic attachment meshes. Shadows retain the procedural weapon
silhouette, not detailed GLB rail/sight geometry.

The optional source sidearm is untextured and imports at about 2.715 m. Its
physical-size reference and grip fit need authoring calibration before it can
replace the sidearm placeholder. The immutable F fixture is unchanged.

## Checks

Headless foundation tests cover measured ADS/bore alignment, orthonormal ready
transforms, muzzle length, clearance, exact replay and every reload interruption.
The explicit GPU test (requires the private export) checks 4K maps/tangents,
model-to-authority transform agreement, visible magazine removal, normal-map
response, immutable arsenal, opt-out/missing-asset/equipment fallback and cleanup:

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
