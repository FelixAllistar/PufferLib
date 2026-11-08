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
- groups: packs and herds, shown on the HUD; a call (G) forms or joins a group and rallies its members;
- abilities on keys 1-3 (pounce and latch, fear roar, tail slam, charge, hunker, dodge leap and more), with stun, fear, slow and mark effects;
- server quests at named places (drink, graze, hunt, patrol, defend, group quests) that pay growth and marks;
- saved characters, and nests where a group's young hatch (see `docs/progression.md`).

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
./build/cretaceous/play join 192.168.1.20   # client (--species stego asks the server to spawn you as one)
```

```bash
./build/cretaceous/play capture build/cretaceous/shots --size 1280 720   # review stills
```

```bash
./build/cretaceous/play capture build/cretaceous/shots --shot 'dino_*'   # only the dinosaur shots
```

Camera-path videos for reviewing motion (silent, windowless; needs `ffmpeg`). Paths: `flyover`, `forest_walk`, `tp_run`, `dusk_night`, `rain_in`, `turn`, `grass_meadow`, `grass_river`, `grass_push` (a raptor and a stegosaur walking through tall grass), or `all`:

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
| Left mouse | Attack, aimed where the camera looks within the neck arc (bite; the stegosaur thrusts its tail behind it, the pachycephalosaur headbutts). Never roots you; a click up to 0.2 s early is queued |
| Right mouse | Secondary attack: tail swipe (tyrannosaur, pachycephalosaur, sauropelta; strong knockback), wide tail sweep (stegosaur), slash or kick in passing at a run (raptor, gallimimus) |
| 1 2 3 | Abilities (slot 3 is bought with marks in the Esc menu) |
| N (hold) | Build a nest (a fed adult) |
| Esc | Menu: resume, species, respawn, ability 3, settings, credits, quit (solo play pauses; clients ask the server) |
| Enter | Respawn now, when dead (clients too) |
| R | Autorun |
| Mouse, wheel | Look and zoom (0.6x to 2.5x of a close over-the-back view); the cursor is captured in play, Tab or a menu frees it. The camera never looks top-down and its boom stops short of terrain, water, rocks, logs and trunks. `CRET_MOUSE_RAW=0` turns raw mouse motion off, `CRET_MOUSE_DEBUG=1` prints the mouse deltas |
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

Training (contract version 5, `sim.h`):
- Observations (contract 4 and 5, 243 floats): heading, velocity, position, terrain, water, time of day; species (one-hot over 6), alive, health, hunger, thirst, growth, bleeding, attack ready, forage underfoot, nearest carcass, hit flinch, broken bone, group size, rally point; a 7x7 terrain and water patch; the 6 nearest animals with their diet, relative size, health, same-group and same-species flags.
  Contract 4 appends 46 floats: status effects, ability cooldowns and ready flags, the quest direction and progress, the own nest, and per neighbour stunned, afraid, latched and marked (`docs/progression.md`).
- Actions (contract 5, heads `{5, 5, 3, 2, 8, 5}`): throttle, turn, strafe, jump, an action head (none, attack, eat or drink, call, ability 1, 2, 3, secondary attack; eat turns into drink when facing water) and the head aim (-1, -0.5, 0, 0.5, 1 of the species' neck arc; the stegosaur's tail arc behind it). Every RL agent has all its species' abilities. Contract 4 checkpoints do not load (the action heads changed).
- Reward per tick, with weights in `[env]`: alive, food and water gained, damage dealt (fraction of the victim's health), damage taken, kills, death, growth, quests completed.
- A death is terminal for that agent; it respawns as a new animal (`respawn_ticks = 1`).
- `perf` is the fraction of agents that survive the whole episode.


`ocean/cretaceous/Makefile` builds the same binaries for quick iteration (`make player bake CC=clang`).
It also builds the offline `bake` tool, which writes terrain previews.
`make net-test` builds and runs a headless loopback test of the respawn and species protocol (`tests/test_net.c`, no window or GPU; the first run bakes a small 256² world).

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
| **net** | `net.[ch]`, `netclient.c`, `netsim.[ch]` | sim, ENet | Per-client delta-compressed snapshots with interest management, sequenced inputs, client-side prediction with reconciliation, interpolated remote agents, respawn/species requests answered once settled, abilities, statuses, quests, nests and character names, a link conditioner; versioned bit-packed bounds-checked codec (version 8). [docs/net.md](docs/net.md) |
| **abilities** | `abilities.[ch]` | sim | Ability table, loadouts and unlocks, status effects (stun, fear, slow, latch, defensive, marked). |
| **progress** | `progress.[ch]` | sim, world | Named places from the world data, quests, nests and eggs. |
| **character** | `character.[ch]` | sim, abilities | Saved characters in `~/.config/cretaceous/characters/<name>.ini`. Player and server only. |
| **gfx** | `render/gfx.[ch]` | raylib (window and GL loader only), GL 3.3 | Shader loading with recursive `#include`, render targets and texture helpers. |
| **view** | `render/view.[ch]` | gfx, terra | Terrain, water, atmosphere, volumetric clouds and post-processing (details below). |
| **veg** | `render/veg.[ch]` | gfx, terra | Procedural plants, impostors and instanced drawing (details below). |
| **csm** | `render/csm.[ch]`, `shaders/csm.glsl` | gfx | Cascaded sun shadow maps: plants, props and agents cast; terrain, plants, props and water receive. |
| **dino** | `render/dino.[ch]`, `shaders/dino.vs/fs` | gfx, sim | Skinned dinosaurs from `assets/dinos/*.dino` (details below). |
| **player** | `cretaceous.c` | all | Solo, host, join, server and capture modes. |
| **capture** | `capture.[ch]` | view, sim | Scripted camera paths rendered frame by frame and piped to ffmpeg (`capture --path`). |
| **audio** | `audio.[ch]` | raylib audio, sim | Ambience beds mixed from the world around the listener, positional footsteps, calls, attacks, hits, eating, drinking and deaths, and menu sounds (details below). Player only; the trainer never links it. |
| **menu** | `menu.[ch]` | hud | Title screen over a flyover of the island, settings (quality, render scale, display, interface size, mouse, volumes, key list) saved to `~/.config/cretaceous/settings.ini`. |
| **hud** | `hud.[ch]` | raylib, sim | Health, stamina, hunger and thirst bars, growth, injuries, compass, minimap and island map, group panel, group-coloured nameplates, rally marker, ability bar with cooldowns and status chips, quest tracker and markers, death screen, pause menu and credits (read from `assets/dinos/CREDITS.md`). Laid out for 1080 lines and scaled to the window height (1.0 at 1080p), times the Interface size setting (50% to 200%; above 100% capped so the HUD keeps 600 layout lines and the menu screens 880); fonts re-rasterise at larger sizes above 1080p. |
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
- instanced drawing with wind and shared lighting (`veg_light.glsl`);
- tall grass, sedge and reeds that animals push through (below).

**Tall grass** (`VEG_GRASS` in `render/veg.c`): clumps of arching ribbons textured with blade-photo strips (atlas layers `L_GRASS`, `L_SEDGE`, `L_REED`, pinched at the base into a tuft).
- Variants: meadow grass (0.8–1.2 m), tall grass (1.2–1.7 m), a sedge fountain, and reeds with a sedge skirt (1.5–2 m) that may stand in 15 cm of water.
- Placement is regenerated around the camera on a 0.8 m lattice. Density follows open living ground (fern prairie, soil, mud, some moss and litter; never rock, scree or sand), thins under a closed canopy, and is highest at lake and river edges, where sedge and reeds take over. A ~6 m tussock noise inside ~40 m meadow patches makes it clumpy.
- Drawn out to 62% of the ground-cover range (56 m on high, 45 m medium, 33 m low), full mesh within 15 m (scaled by preset) and a 16-triangle mesh beyond. It shrinks a little and dithers out over the last 30%. Casts into the first shadow cascade within 28 m.

**Bending** (`shaders/veg.vs`): every ground-cover instance carries a stiffness (grass and sward 1, ferns 0.75, horsetails 0.6, scrub 0.3; trees 0) and its height.
Each point swings about the ground below it, more toward the tip, and its normal turns with it.
- Wind: gust bands roll downwind at about 7 m/s and waver across the field, so a meadow leans and sways in waves; wind is stronger under an overcast sky.
- Animals: the trample map (`render/trample.h`, `shaders/trample.fs`) is a 256² RGBA16F target at 0.4 m per texel (102 m) that scrolls with the camera.
  Each frame one pass stamps the contact-shadow bodies (the 16 nearest animals within 45 m), pushing the cover outward from each spine; bigger bodies push harder (a raptor about two thirds of a stegosaur).
  Blades follow within about 0.1 s, spring most of the way back over about half a second, and a trail stays half flattened, fading over about 3.5 s.

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

Third-person camera (`update_camera` in `cretaceous.c`): the pivot sits just behind the shoulders at 1.05x the animal's height, and the boom is 1.5 body lengths at zoom 1, so every species fills the frame alike.
The look pitch stays between 0.55 rad down and 0.45 rad up.
The boom is a 0.3 m sphere swept from the pivot. It stops at prop colliders (Box3D) and tree trunks (`cret_veg_cast_trunks`), pulls in at once and eases back out (2.5/s).
Terrain and water lift the lens instead of shortening the boom, unless a ridge would hide the animal.
Nothing turns see-through; only leaves within 0.6 m of the lens are cut.

Mouse look: the cursor is captured in play. Raw motion is on where GLFW supports it, except under WSL, where WSLg's absolute pointer makes raw deltas into positions.
The first frames after a capture, a focus change or a resize are dropped. A jump of more than a quarter of the window in one frame is thrown away, and three in 2 s turn raw motion off.
Sensitivity 1 is 0.0025 rad per pixel.
Presses (attacks, call, jump, abilities) are latched until the next 30 Hz tick, so a click in a frame without a tick still counts.

Combat: attacks aim where the camera looks, within the species' neck arc (the stegosaur's tail behind it). They keep 70 to 92% of the gait until the blow lands and never root the animal.
The secondary attack (right mouse) is a tail swipe with mass-scaled knockback for the tyrannosaur, pachycephalosaur and sauropelta, a wide tail sweep for the stegosaur, and a quick pass-through slash or kick for the raptor and gallimimus.
The head turns toward the aim. On the move, attack clips play over the neck and head only, and tail swipes are procedural (`render/dino.c`).

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
It takes the water velocity (below) for its river normals.
Nothing is simulated at runtime, so a basin of any shape fills correctly, provided terra assigned it a level.

## How water flows through river mouths

terra stores one D8 receiver direction per cell. Shading water straight from it gave a seam at every river mouth:
- bank cells drain sideways into the channel, so the two halves of a river ran toward each other;
- the flow stopped dead at the edge of the lake or sea, and the shader switched from river ripples to wind ripples there.

`render/water_flow.h` is a single-header, dependency-free builder for a smooth velocity field. It takes:
- a kind per cell: dry, moving (river) or still (lake, sea);
- a downstream direction for each moving cell;
- a weight for each moving cell (the renderer uses √discharge).

`water_flow_build(n, kind, dir, weight, &params, out)` writes two floats per cell, direction × strength (|v| ≤ 1):
1. **Rivers.** Each cell takes the weighted mean direction over a window (`smooth_radius`, 4 cells). The thalweg dominates, and the banks' sideways components cancel. Strength is 1.
2. **Still water near a river.** Within `spread` cells (12) of a river, the river's velocity diffuses outward, fanning out past a mouth or converging on an outlet. Its strength falls smoothly to zero.
3. **Smoothing.** A 3×3 blur over water, then a `dilate` (2 cells) into dry ground so bilinear filtering at the banks stays smooth.

`water.fs` reads the field from `t_misc.xy`. It advects ripples with a two-phase flow map: two copies half a cycle apart, each reset while it is invisible, so turning or diverging flow never tears. It blends river ripples into still-water chop by |v|, and fades the silt colour and the end of the breakers closer in, so a mouth has no boundary.
The build takes about 0.2 s for 2048². `CRET_WATER_FLOW=0` shows the raw D8 directions for comparison.

## Materials and lighting

The look is stylised rather than full PBR, and every lit surface stays matte:
- **Leaves and fronds** (`veg_light.glsl`) have no specular lobe. A card or strip has a single normal, so any highlight lights the whole strip at once and reads as plastic.
  - Normals are bent outward from the crown, and lit with a squared half-Lambert, so a crown shades as one soft volume.
  - Sun shining through a blade glows yellow-green when backlit.
  - Sky and ground light come from a hemisphere, scaled by the baked crown occlusion and filtered green through the crown.
  - Each leaf varies its value by ±12%, and ground cover darkens toward its base.
  - Rain adds only a faint, broad sheen of grey sky.
- **Rock, wood and bark** (`props.fs`) keep a dim, broad lobe: roughness at least 0.7 when dry and about 0.45 under rain, with roughness-limited Fresnel, plus a faint wet sheen in rain.
- **Dinosaur skin** (`dino.fs`) gets a soft wrap and a faint rough Fresnel rim. Its lobe has roughness at least 0.65, or 0.35 when wet (in rain or below the waterline).
- **Ground** (`terrain.fs`) has roughness at least 0.75, lowered only in the damp band at the water's edge and in rain.

**Contact shadows** (`csm.glsl` `contact_occlusion`, `cret_csm_set_contacts`): the 16 nearest animals within 45 m are passed as ellipses (position, yaw, half length, half width, height).
Terrain, plants and props darken their sky light under and around each animal, so animals sit in the grass under an overcast sky and at night too. The sun shadow maps carry the direct shadow, and the contact shadows work with them off.

## Debug knobs

| Variable | Effect |
|---|---|
| `CRET_TERRA="k=… uplift=… diffusion=… years=…"` | Overrides bake parameters (also `bake --set`). |
| `TERRA_NO_DETAIL`, `TERRA_NO_THERMAL`, `TERRA_NO_SLIDE`, `TERRA_NO_BEACH`, `TERRA_DEBUG` | Disables bake stages and prints statistics. |
| `TERRA_ACRIT` | Channel-initiation area in m² (default 4000). |
| `CRET_VEG_NEAR` | Mesh/impostor switch distance in metres (default 150). |
| `CRET_DUMP_ATLAS`, `CRET_DUMP_IMP` | Writes the foliage atlas or impostor layers as PNGs. |
| `CRET_MOUSE_RAW=0/1` | Forces raw mouse motion off or on (default: on where supported, off under WSL). |
| `CRET_MOUSE_DEBUG=1` | Prints every mouse delta and every dropped one. |
| `CRET_SHADOWS` | Sun shadow quality: 0 off, 1 low (3 cascades at 1024² to 240 m), 2 high (default; 4 at 2048² to 800 m). |
| `CRET_SHADOW_RES`, `CRET_SHADOW_CASCADES`, `CRET_SHADOW_FAR` | Override the shadow map size, cascade count and last cascade distance (m). |
| `CRET_SHADOW_DEBUG` | Prints shadow GPU ms per cascade, caster counts and whole-frame GPU ms every 24 frames. Do not combine with `CRET_PROFILE` or `CRET_PROP_DEBUG`, because elapsed-time queries cannot nest. |
| `CRET_SHADOW_VIS=1` | Receivers show the shadow term tinted by cascade (red, green, blue, yellow). |
| `CRET_SHADOW_NUDGE="dx dy dz"` | Moves only the cascade fit. With texel snapping, the image must not change (stability test). |
| `CRET_PRESET=low\|medium\|high` | Quality preset (default high): render scale, shadow maps, cloud steps, plant and prop ranges, terrain parallax. Single knobs such as `CRET_SHADOWS`, `CRET_CLOUD_QUALITY` and `CRET_RENDER_SCALE` override it. |
| `CRET_GPU_PROFILE=1` | Per-pass GPU and CPU ms (timer queries), printed per shot by `capture` with `CRET_CAPTURE_FRAMES=N`. "present" is the swap and the time until the next frame. Do not combine with the other GPU timers above. |
| `CRET_TEX_COMPRESS=0` | Keeps terrain and prop textures uncompressed (default BC3/DXT5). |
| `CRET_NO_CLIP_CONTROL=1` | Tests the reverse-Z fallback for GL without `ARB_clip_control`. |
| `CRET_VEG_DEBUG=1` | Also prints plant instances and triangles per frame, and the tall-grass scatter (clumps, ms). |
| `CRET_GRASS=0` | No tall grass, and no bending from wind gusts or animals. |
| `CRET_GRASS_RANGE=M` | Tall-grass draw range in metres on the high preset (default 56). |
| `CRET_TRAMPLE_DUMP=file.ppm` | Every 30th frame writes the trample map (red, green: bend; blue: trail). |
| `CRET_WEATHER=0\|1\|2` | Forces clear, overcast or rain. By default the weather follows the cloud cover: above about 0.7 the sky greys over, above about 0.9 it rains (streaks, haze, wet surfaces). |
| `CRET_CLOUD_CB=0` | Marches every half-res cloud pixel each frame instead of a quarter of them (checkerboard plus history; about 1 ms cheaper on open views). |
| `CRET_SHADOW_STAGGER=0` | Renders every shadow cascade each frame (default: the two farthest alternate). |
| `CRET_FLUSH=N` | Bitmask of the mid-frame `glFlush` calls (1 after shadows, 2 after terrain; default 3). |
| `CRET_DINO_TEX=N` | Caps dinosaur texture size (for example 1024). |
| `CRET_NO_PROP_COLLIDERS=1` | Props stop blocking movement. |
| `CRET_NO_DINOS=1` | Draws agents as capsules instead of dinosaur models. |
| `CRET_SHOT_VARIANTS="noon:0.5:0.3,gold:0.70:0.35"` | `capture` stills: renders every selected shot once per `tag:time_of_day:cloud_cover`, as `NAME_TAG.png`. |
| `CRET_WATER_FLOW=0` | Raw per-cell D8 river directions instead of the smoothed flow field (`render/water_flow.h`). |
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
