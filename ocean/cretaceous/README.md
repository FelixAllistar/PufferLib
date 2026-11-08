# Cretaceous

A survival game where you play as a dinosaur, in the spirit of Path of Titans. It doubles as a multi-agent RL environment.
The trainer, the dedicated server, the listen host and solo play all run the same fixed-step `CretSim`.
Humans, network peers, scripted bots and policies all drive agents through `CretInput`.
Rendering never feeds back into the sim.

Status (2026-11-07): six playable species are skinned, animated models (`render/dino.c`):
Velociraptor, Stegosaurus, Tyrannosaurus, Pachycephalosaurus, Sauropelta and Gallimimus.
Survival works end to end:
- health, bleeding, hunger, thirst and stamina;
- bites and tail swings with reach, cooldown, knockback and a flinch;
- death leaves a carcass that carnivores eat and that rots away; the animal respawns;
- growth from hatchling to adult changes size, speed, health and damage;
- broken bones from heavy hits and falls (slow, no sprint or jump, heal faster at rest);
- groups: packs and herds, shown on the HUD; a call (G) forms or joins a group and rallies its members.

| Species | Role |
|---|---|
| Velociraptor | Fast pack hunter (packs of 4); kills juveniles, sub-adults and the injured |
| Stegosaurus | Big herd grazer; turns its tail on predators |
| Tyrannosaurus | Lone apex predator |
| Pachycephalosaurus | Herd grazer; headbutts knock back and can break bones, but bleed little |
| Sauropelta | Armoured: blunts bites, resists bleeding, spikes hurt attackers from the front; slow |
| Gallimimus | Fastest animal; flees and never fights |

Herbivores graze where plants are thick, drink, keep to the herd and flee together. Stegosaurs and sauropeltas stand and fight; a pachycephalosaur fights a lone predator.
Predators hunt when hungry, eat the kill or scavenge, drink and rest. Raptors call their pack, stalk, then flank from both sides; they skip prey they cannot catch or overpower.
Big rocks, logs, stumps, roots and coast and cliff pieces block movement.
Policies observe all of this and get a survival reward (see Training).

## Build and run

From the repository root:

```bash
./build.sh cretaceous build/cretaceous/play --cpu
```

```bash
./build/cretaceous/play                     # title screen: Play, Host, Join, Settings, Credits
```

```bash
./build/cretaceous/play play                # straight into solo play
```

```bash
./build/cretaceous/play play --species stego --day-minutes 30   # species; day length (weather on unless --clouds C)
```

```bash
./build/cretaceous/play host --port 7777    # listen server, you are slot 0
```

```bash
./build/cretaceous/play join 192.168.1.20   # client
```

```bash
./build/cretaceous/play capture build/cretaceous/shots --size 1280 720   # review stills
```

```bash
./build/cretaceous/play capture build/cretaceous/shots --shot 'dino_*'   # only the dinosaur shots
```

Camera-path videos for reviewing motion (silent, windowless; needs `ffmpeg`). Paths: `flyover`, `forest_walk`, `tp_run`, `dusk_night`, `rain_in`, `turn`, or `all`:

```bash
./build/cretaceous/play capture /mnt/d/puffer_media/cretaceous/v1 --path all --size 1280 720
```

Controls:

| Key | Action |
|---|---|
| W A S D | Move relative to the camera at a trot; hold Ctrl to walk |
| Shift | Sprint (uses stamina) |
| Space | Jump (raptor only) |
| E (hold) | Eat, or drink when facing water |
| G | Call: form or join a group with nearby animals of your species, rally your group |
| M | Island map (the corner minimap shows 900 m around you) |
| Left mouse | Attack (bite; the stegosaur swings its tail behind it, the pachycephalosaur headbutts) |
| Esc | Menu: resume, species, respawn, credits, quit (solo play pauses; clients ask the server) |
| Enter | Respawn now, when dead (clients too) |
| R | Autorun |
| Mouse, wheel | Orbit and zoom the camera; Tab frees the cursor |
| F | Free camera (Shift fast, E and Q up and down) |
| T (hold) | Time 60x |
| C, + and - | Clouds on or off, cloud cover |
| F1, F2, F5 | Help, wireframe, reload shaders |

Builds the trainer. The OUT name keeps the shared `./puffer` intact:

```bash
./build.sh cretaceous puffer_cret
```

Train with `config/cretaceous.ini`:

```bash
./puffer_cret train
```

Training (contract version 2, `sim.h`):
- Observations (contract 3, 197 floats): heading, velocity, position, terrain, water, time of day; species (one-hot over 6), alive, health, hunger, thirst, growth, bleeding, attack ready, forage underfoot, nearest carcass, hit flinch, broken bone, group size, rally point; a 7x7 terrain and water patch; the 6 nearest animals with their diet, relative size, health, same-group and same-species flags.
- Actions: throttle, turn, strafe, jump, and an action head (none, attack, eat or drink, call; eat turns into drink when facing water).
- Reward per tick, with weights in `[env]`: alive, food and water gained, damage dealt (fraction of the victim's health), damage taken, kills, death, growth.
- A death is terminal for that agent; it respawns as a new animal (`respawn_ticks = 1`).
- `perf` is the fraction of agents that survive the whole episode.


`ocean/cretaceous/Makefile` builds the same binaries for quick iteration (`make player bake CC=clang`).
It also builds the offline `bake` tool, which writes terrain previews.

The first run bakes the world, which takes about 45 s at 2048² on 4 threads.
The result is cached in `build/cretaceous/worlds/`. Changing `TERRA_VERSION` or any parameter re-bakes it.

On WSL, Mesa silently falls back to llvmpipe (CPU) unless `GALLIUM_DRIVER=d3d12` is set.
The player sets it itself when `/dev/dxg` exists. That made capture about 25x faster.

## Modules

Each module is plain C99/C11 with a narrow header, so it can become its own repo later.

| Module | Files | Depends on | What it does |
|---|---|---|---|
| **terra** | `terra/terra.[ch]` | libc, OpenMP (optional) | Offline world bake (see the pipeline below). Outputs one `.terra` file holding height, water level and kind, flow, soil, temperature, 8 material splats and 8 vegetation layers. |
| **world** | `world.[ch]` | terra, Box3D | Loads or bakes the cache, builds the Box3D height-field collider, and loads the prop collision shapes (hulls from each prop's LOD2, meshes for coast and cliff pieces). |
| **props_place** | `render/props_place.[ch]` | terra | Deterministic prop scatter shared by the renderer and the colliders, so what you see is what blocks you. |
| **sim** | `sim.[ch]` | world | Authoritative 30 Hz sim: species table (size, gaits, turning, stamina, jump), agents, actions (eat, drink, call, attack, rest), swimming, day cycle, observations, scripted herd and predator bots. Prop colliders stream in per 64 m tile around agents. |
| **net** | `net.[ch]` | sim, ENet | Snapshots from server to clients (with groups, broken bones and rally points), inputs and respawn/species requests from clients to server; versioned bounds-checked codec (version 5, adds weather). |
| **gfx** | `render/gfx.[ch]` | raylib (window and GL loader only), GL 3.3 | Shader loading with recursive `#include`, render targets and texture helpers. |
| **view** | `render/view.[ch]` | gfx, terra | Terrain, water, atmosphere, volumetric clouds and post-processing (details below). |
| **veg** | `render/veg.[ch]` | gfx, terra | Procedural plants, impostors and instanced drawing (details below). |
| **csm** | `render/csm.[ch]`, `shaders/csm.glsl` | gfx | Cascaded sun shadow maps: plants, props and agents cast; terrain, plants, props and water receive. |
| **dino** | `render/dino.[ch]`, `shaders/dino.vs/fs` | gfx, sim | Skinned dinosaurs from `assets/dinos/*.dino` (details below). |
| **player** | `cretaceous.c` | all | Solo, host, join, server and capture modes. |
| **capture** | `capture.[ch]` | view, sim | Scripted camera paths rendered frame by frame and piped to ffmpeg (`capture --path`). |
| **audio** | `audio.[ch]` | raylib audio, sim | Ambience beds mixed from the world around the listener, positional footsteps, calls, attacks, hits, eating, drinking and deaths, and menu sounds (details below). Player only; the trainer never links it. |
| **menu** | `menu.[ch]` | hud | Title screen over a flyover of the island, settings (quality, render scale, display, interface size, mouse, volumes, key list) saved to `~/.config/cretaceous/settings.ini`. |
| **hud** | `hud.[ch]` | raylib, sim | Health, stamina, hunger and thirst bars, growth, injuries, compass, minimap and island map, group panel, group-coloured nameplates, rally marker, death screen, pause menu and credits (read from `assets/dinos/CREDITS.md`). Laid out for 1080 lines and scaled to the window height (1.0 at 1080p), times the Interface size setting (50% to 200%; above 100% capped so the HUD keeps 600 layout lines and the menu screens 880); fonts re-rasterise at larger sizes above 1080p. |
| **binding** | `cretaceous.h` | sim | PufferLib env: every agent is a learner. |

The terra bake pipeline:
- tectonic plates and uplift;
- multigrid stream-power erosion with a channel threshold, sediment deposition, implicit diffusion and mass-conserving talus;
- droplet erosion and lakes from a priority flood;
- coastal flats: on about half the coastline, land near the sea is lowered onto a gentle beach profile (35–110 m wide) and the seabed offshore is raised into a shallow shelf;
- rivers carved along flow;
- surface materials and vegetation from moisture, temperature, slope and curvature.

The view module covers:
- quadtree-LOD terrain with a B-spline-prefiltered height texture, so the smooth surface passes through the baked samples;
- height-blended triplanar materials and horizon-angle terrain shadows;
- precomputed sky (transmittance and sky-view LUTs);
- volumetric clouds with a weather map, checkerboarded half-res marching and temporal accumulation;
- night lit by the full moon and graded day-for-night (dark, blue, desaturated), with a moon disk and stars;
- weather driven by the cloud cover: overcast deck, grey sky light, rain streaks, haze and wet surfaces;
- water;
- bloom and tonemapping;
- reverse-Z depth in a float buffer (no shader writes depth), so plants get a depth prepass and terrain and plants shade each pixel once.

The veg module covers:
- a procedural foliage atlas and procedural species meshes;
- deterministic per-tile scatter from the terra vegetation layers;
- 8-view impostors baked in-engine, with albedo plus normal and AO;
- instanced drawing with wind and shared lighting (`veg_light.glsl`).

The dino module covers:
- GPU skinning: joint matrices for every agent in one float texture, one instanced draw per species, LOD and material, also into the shadow cascades;
- three LODs by distance relative to body length;
- locomotion clips blended and phase-synced by ground speed, with playback rate matched to stride; jump, eat, drink, call and attack crossfade in;
- the body pitches and rolls with the ground and leans into turns;
- lighting shared with the props (sun, cascades, sky ambient under the canopy, wet skin below the waterline).

`tools/dino_convert.py` (headless Blender) turns an animated glTF into a `.dino` file:
- skeleton pruned to weighted joints, 4 weights per vertex;
- model scaled to a given length, facing +x, feet on the ground;
- clips resampled at 30 Hz, with root motion removed and the ground speed measured from the planted foot;
- LOD0 within a triangle budget, decimated LOD1 and LOD2;
- 2K body textures (albedo, normal, AO, roughness, metal).

In third person, plants between the camera and the player dither out.

These pieces could stand alone as open-source repos: **terra** (terrain baker), **sky + clouds** (the view atmosphere passes) and **veg** (procedural plants and impostors).
Once a second game uses them, `gfx` would become the shared base library.

## Sound

`audio.c` plays everything through raylib's audio (miniaudio). It reads the sim and never changes it.
- **Ambience** is eight looped beds whose volumes follow the world around the listener, sampled every quarter second on rings out to 260 m:
  - forest birds and insects by day, and cicadas on warm low ground;
  - crickets and night insects after dusk, and frogs near rivers and lakes;
  - wind with altitude, open ground and bad weather;
  - surf near the sea, a stream near rivers, and rain from the sky's cloud cover.
  The beds cross-fade over a couple of seconds, and drop under water.
- **Footsteps** fall once per stride from each animal's speed and size, and use the ground under it (fern and litter, dirt and mud, rock and scree, sand, shallow water). Heavy animals add a low thud, pitched by mass. Footsteps carry 50 m for a raptor and about 250 m for a tyrannosaur.
- **Calls** (G) are per species, pitched up for juveniles. Attacks growl (carnivores, the pachycephalosaur) and bite or swing when the hit lands. Hits thump, eating crunches or chews, drinking laps, and a death cries and falls.
- Positional sounds fade with distance from the player's head and pan by bearing from the camera.

Settings has master, ambience, effects and interface volumes.
With no audio device, or without the sound files, the game runs silently.
`CRET_AUDIO_RENDER=out.wav` mixes in software and writes everything heard to a file (tests without speakers). `CRET_AUDIO=0` turns sound off, `CRET_AUDIO_LOG=1` prints each sound.

All sounds are built from CC0 Freesound recordings by `python3 -I ocean/cretaceous/tools/fetch_audio.py` (needs ffmpeg and sox; about 6.5 MB of ogg).
`assets/audio/SOURCES.md` lists every source and what was made from it.
The dinosaur calls are animal and creature recordings slowed (pitch and formants drop together, which reads as a larger body), filtered and layered.

## Weather and days

Cloud cover is the sim's own state, so clients see the host's weather (snapshot version 5).
With `weather` on (the player's default), it drifts through spells of fair sky, overcast and rain, each 4 to 15 minutes long, easing between them over a few minutes. It has its own random stream, so it never changes what the animals do.
`--clouds C` fixes the cover; `+` and `-` in game do the same.
`--day-minutes M` sets the day length (default 20 minutes).
The trainer keeps `weather = 0` and reads `day_seconds` from `config/cretaceous.ini`.

## How water fills any shape

terra already knows the water *surface level* of every wet cell: sea level 0, the lake spill level from the priority flood, or the river surface from the nearest centreline.
The renderer turns that into one continuous water sheet:

1. **Level texture (RG32F).** R is the water level. G is a wet mask (1 where terra marked water).
2. **Extend the level.** Dry cells copy the level of the nearest wet cell (BFS, 10 cells). Farther away, R is ground − 30 m.
3. **Draw a sheet.** A grid mesh is displaced to R.
   - The sheet is continuous, so it has no seams between lakes, rivers and sea.
   - The terrain depth test hides the sheet wherever ground is above it, so the shoreline is the exact intersection of the water plane and the terrain, at any shape and resolution.
4. **Discard what shouldn't show.** The water shader drops a fragment if it is far below ground (`depth < −1.5 m`). It also drops it if it is outside the wet mask and above ground, which stops extended levels from flooding dry basins.

The shader takes depth (surface level minus ground height) for its shore foam and refraction fallback.
It takes flow direction and speed from the bake for its river normals.
Nothing is simulated at runtime, so a basin of any shape fills correctly, provided terra assigned it a level.

## Debug knobs

| Variable | Effect |
|---|---|
| `CRET_TERRA="k=… uplift=… diffusion=… years=…"` | Overrides bake parameters (also `bake --set`). |
| `TERRA_NO_DETAIL`, `TERRA_NO_THERMAL`, `TERRA_NO_SLIDE`, `TERRA_NO_BEACH`, `TERRA_DEBUG` | Disables bake stages and prints statistics. |
| `TERRA_ACRIT` | Channel-initiation area in m² (default 4000). |
| `CRET_VEG_NEAR` | Mesh/impostor switch distance in metres (default 150). |
| `CRET_DUMP_ATLAS`, `CRET_DUMP_IMP` | Writes the foliage atlas or impostor layers as PNGs. |
| `CRET_SHADOWS` | Sun shadow quality: 0 off, 1 low (3 cascades at 1024² to 240 m), 2 high (default; 4 at 2048² to 800 m). |
| `CRET_SHADOW_RES`, `CRET_SHADOW_CASCADES`, `CRET_SHADOW_FAR` | Override the shadow map size, cascade count and last cascade distance (m). |
| `CRET_SHADOW_DEBUG` | Prints shadow GPU ms per cascade, caster counts and whole-frame GPU ms every 24 frames. Do not combine with `CRET_PROFILE` or `CRET_PROP_DEBUG`, because elapsed-time queries cannot nest. |
| `CRET_SHADOW_VIS=1` | Receivers show the shadow term tinted by cascade (red, green, blue, yellow). |
| `CRET_SHADOW_NUDGE="dx dy dz"` | Moves only the cascade fit. With texel snapping, the image must not change (stability test). |
| `CRET_PRESET=low\|medium\|high` | Quality preset (default high): render scale, shadow maps, cloud steps, plant and prop ranges, terrain parallax. Single knobs such as `CRET_SHADOWS`, `CRET_CLOUD_QUALITY` and `CRET_RENDER_SCALE` override it. |
| `CRET_GPU_PROFILE=1` | Per-pass GPU and CPU ms (timer queries), printed per shot by `capture` with `CRET_CAPTURE_FRAMES=N`. "present" is the swap and the time until the next frame. Do not combine with the other GPU timers above. |
| `CRET_TEX_COMPRESS=0` | Keeps terrain and prop textures uncompressed (default BC3/DXT5). |
| `CRET_NO_CLIP_CONTROL=1` | Tests the reverse-Z fallback for GL without `ARB_clip_control`. |
| `CRET_VEG_DEBUG=1` | Also prints plant instances and triangles per frame. |
| `CRET_WEATHER=0\|1\|2` | Forces clear, overcast or rain. By default the weather follows the cloud cover: above about 0.7 the sky greys over, above about 0.9 it rains (streaks, haze, wet surfaces). |
| `CRET_CLOUD_CB=0` | Marches every half-res cloud pixel each frame instead of a quarter of them (checkerboard plus history; about 1 ms cheaper on open views). |
| `CRET_SHADOW_STAGGER=0` | Renders every shadow cascade each frame (default: the two farthest alternate). |
| `CRET_FLUSH=N` | Bitmask of the mid-frame `glFlush` calls (1 after shadows, 2 after terrain; default 3). |
| `CRET_DINO_TEX=N` | Caps dinosaur texture size (for example 1024). |
| `CRET_NO_PROP_COLLIDERS=1` | Props stop blocking movement. |
| `CRET_NO_DINOS=1` | Draws agents as capsules instead of dinosaur models. |
| `CRET_PATH_STILLS=N` | `capture --path` also writes every Nth frame as a PNG next to the video (default 30, 0 off). |
| `CRET_PATH_T0`, `CRET_PATH_T1` | Renders only that part (0..1) of each camera path. |
| `CRET_FFMPEG=PATH` | The ffmpeg binary `capture --path` pipes frames into (default `ffmpeg`). |

## Assets

Terrain materials are CC0 photo/procedural sets from ambientCG and Poly Haven, listed in `assets/textures/SOURCES.md`.
To re-download them at 2K, run `python3 -I ocean/cretaceous/tools/fetch_textures.py`. The script writes albedo, normal, roughness, height and AO maps.
The renderer loads them at 2K. For low-memory or mobile builds, use `CRET_MAT_SIZE=1024`.
Within about 35 m of the camera, the dominant ground material gets parallax occlusion and sun self-shadowing.
Plant geometry is procedural, so it stays dynamic.
Dinosaurs are CC BY 4.0 models, credited in `assets/dinos/CREDITS.md`.
The converted files are not committed. To rebuild them, put the GLBs in a folder and run `DINO_SRC=<folder> ocean/cretaceous/tools/fetch_dinos.sh` (about 2 minutes per species).
Reference models in `Downloads/dino-models` follow their CREDITS file. The `_PLACEHOLDER_RIPS_do_not_ship` folder must never be committed or shipped.
