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

Movement (adults; `CRET_SPECIES_TABLE` in `sim.c`). Sprints sit at the high end of the biomechanical estimates and keep the predator-to-prey ratios. Mass sets how fast an animal speeds up (falling off toward its sprint), brakes and grips a turn; at speed the turning circle widens (yaw rate is at most grip / speed).

| Species | Walk / trot / sprint (m/s) | Accel / brake / grip (m/s²) | 90% of sprint | Stop from sprint | Turning radius at sprint |
|---|---|---|---|---|---|
| Velociraptor | 2.2 / 4.6 / 8.6 | 9 / 12 / 10 | 1.1 s | 3.0 m | 7 m |
| Stegosaurus | 2.2 / 3.4 / 4.6 | 2 / 3 / 3 | 2.7 s | 2.2 m | 7 m |
| Tyrannosaurus | 1.5 / 3.2 / 6.4 | 2.5 / 3.5 / 3.5 | 2.9 s | 5.8 m | 12 m |
| Pachycephalosaurus | 1.7 / 3.7 / 7.0 | 6 / 8 / 7 | 1.3 s | 2.8 m | 7 m |
| Sauropelta | 1.3 / 2.2 / 3.1 | 2 / 3 / 3 | 1.8 s | 1.6 m | 3 m |
| Gallimimus | 2.2 / 5.0 / 9.0 | 8 / 10 / 9 | 1.3 s | 3.9 m | 9 m |

Only the raptor hops: 0.75 m high and about 0.6 s in the air (airborne bodies fall at 1.6 g, so hops are snappy rather than floaty; ability leaps keep 1 g). A landing soaks up to a third of the ground speed. Juveniles are nimbler (up to 1.24x accel and grip) and hop lower. `tests/test_sim.c` checks the hop, the mass table, the turning circles and that client prediction (`cret_sim_move`) follows the sim exactly.

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

```bash
CRET_TIME0=900 ./build/cretaceous/play capture build/cretaceous/shots --shot 'creek_*,sv_umbrella*'   # water after 15 minutes of play; the umbrella araucaria
```

Water review shots: `creek_felix`, `creek_fast` and `creek_meet` (a steep creek at 3783, 1849, its steepest reach and where it meets slower water), `creek_steep` (the biggest steep creek), `river`, `mouth`, `mouth_high`, `lake` and `coast`.
Tall trees: `sv_*` per variant, `sv_*_up` (the crown from below, conifers and palms), `sv_umbrella_side` and `sv_umbrella_far` (crown height, full mesh and LOD), and `tree_felix` (a stand at 5627, 1972).
Plant lineup (`CRET_VEG_LINEUP=1`): every tall kind and variant planted in rows on open ground (other trees cleared within 330 m), with `lu_<kind><v>` (side, eye height), `lu_<kind><v>_up` (from under the crown), and per kind `lu_<kind>_twig` (close), `lu_<kind>_mid` (reduced mesh) and `lu_<kind>_far` (impostors).

### Native Windows build

Under WSL the Linux player cannot lock the pointer, so for play on Windows there is a native `cretaceous.exe`, cross-compiled with zig cc (Zig 0.13.0 in `~/tools/zig-0.13.0`, raylib 5.5 source in `~/tools/raylib-5.5-src`; raylib, Box3D and ENet are built from source once into `build/cretaceous/win-deps/`):

```bash
ocean/cretaceous/tools/build_windows.sh                 # -> build/cretaceous/win/cretaceous.exe
```

```bash
ocean/cretaceous/tools/package_windows.sh               # -> /mnt/d/cretaceous_win (D:\cretaceous_win)
```

The package holds the exe, the shaders, `config/water.ini`, the assets and the world and texture caches at the paths the game opens, plus `Play Cretaceous.bat`, `Host.bat`, `Join.bat`, `README.txt` and `CREDITS.md`. It needs no DLLs beyond Windows' own.
On Windows, settings and characters live in `%APPDATA%\Cretaceous`, the mouse is always captured (no WSL drag scheme), and the Linux-only `GALLIUM_DRIVER` switch is skipped. The POSIX pieces MinGW lacks (pthreads, `clock_gettime`, `nanosleep`, `setenv`, `sysconf`, a replacing `rename`) are shims in `win32/`, used only by that build.
Zig ships no OpenMP runtime for Windows, so the exe runs the OpenMP loops on one thread: a cold start (an empty `build/cretaceous/cache`) or a world bake is slower than on Linux. The package ships both caches, so only changed items rebuild.

Camera-path videos for reviewing motion (silent, windowless; needs `ffmpeg`). Paths: `flyover`, `forest_walk`, `tp_run`, `dusk_night`, `rain_in`, `turn`, `grass_meadow`, `grass_river`, `grass_push` (a raptor and a stegosaur walking through tall grass), `grass_trail` (the same from high behind, showing the lane they leave), or `all`:

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
| F1, F2, F5 | Help, wireframe, reload shaders and `config/water.ini` |

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

The first run bakes the world, which takes about 85 s at 2048² on 4 threads.
The result is cached in `build/cretaceous/worlds/`. Changing `TERRA_VERSION` or any parameter re-bakes it (version 20: broader mountains, see the pipeline below).

The renderer's derived data is cached in `build/cretaceous/cache/` (gitignored): decoded and mipmapped ground, prop and dinosaur materials (BC3 where compressed), the foliage atlas and the terrain textures (b-spline height, horizons, normals, splats, water flow, vegmix).
Each item is keyed by a hash of its inputs (asset bytes, world data, settings that change it) and of the sources that derive it, so editing an asset or the deriving code rebuilds that item; a stale, truncated or corrupt file is a miss and is rewritten. Deleting the directory just rebuilds everything.
Textures are built on the CPU (`render/texbuild.c`: mip chains, sRGB-correct, and BC3 blocks, on threads) and stored from that copy; nothing is read back from the GPU. Reading textures back made Mesa's d3d12 driver (WSL) crash in later allocations, and the threaded encoder is also several times faster than the driver's compression. The tree impostors are baked by the GPU, so they are rebaked each start (about 0.5 s).
The sim (world, Box3D colliders) is built on a thread while the window and GL context come up.
On a GTX 1060 under WSL (`play`, 960x540) a warm start reaches the first frame in about 11 s and a cold one (empty cache) in about 25 s, against about 50 s before. About 6 s of either is the driver compiling shader variants at the first draw, which the app cannot cache on the d3d12 path; `CRET_PROFILE_LOAD=1` prints every stage.

On WSL, Mesa silently falls back to llvmpipe (CPU) unless `GALLIUM_DRIVER=d3d12` is set.
The player sets it itself when `/dev/dxg` exists. That made capture about 25x faster.

## Modules

Each module is plain C99/C11 with a narrow header, so it can become its own repo later.

| Module | Files | Depends on | What it does |
|---|---|---|---|
| **terra** | `terra/terra.[ch]` | libc, OpenMP (optional) | Offline world bake (see the pipeline below). Outputs one `.terra` file holding height, water level and kind, flow, soil, temperature, 8 material splats and 8 vegetation layers. |
| **world** | `world.[ch]` | terra, Box3D | Loads or bakes the cache, builds the Box3D height-field collider, and loads the prop collision shapes from each prop's LOD1: a hull per variant (checked against LOD0 by `test_prop_colliders`), four overlapping slabs for logs and driftwood, meshes for roots, coast and cliff pieces. Tree trunks (conifers, broadleaves, palms, tree ferns, cycads with a stem of 1.2 m or more) are static capsules per 64 m tile (`cret_tree_trunks`), sized from the generator's own species dimensions (`veg_species_dims` in `render/veg_place.h`, which `render/veg_gen.h` builds the meshes from), so every collider has a drawn trunk; ferns, grass, shrubs and low cycads stay walk-through. Bots feel ahead for trunks and rocks (a capsule cast every fourth tick, `avoid_props` in `sim.c`) and veer round them. |
| **props_place** | `render/props_place.[ch]` | terra | Deterministic prop scatter shared by the renderer and the colliders, so what you see is what blocks you. |
| **dynprops** | `dynprops.c` (included by `sim.c`) | sim, Box3D | Movable props: boulders, big stones, logs and driftwood are Box3D dynamic bodies, pushed by animals and crushing them (below). |
| **sim** | `sim.[ch]` | world | Authoritative 30 Hz sim: species table (size, gaits, turning, stamina, jump), agents, actions (eat, drink, call, attack, rest), swimming, day cycle, observations, scripted herd and predator bots. Prop colliders stream in per 64 m tile around agents. |
| **net** | `net.[ch]`, `netclient.c`, `netsim.[ch]` | sim, ENet | Per-client delta-compressed snapshots with interest management, sequenced inputs, client-side prediction with reconciliation, interpolated remote agents, respawn/species requests answered once settled, abilities, statuses, quests, nests and character names, a link conditioner; hit events for combat feedback, movable props, versioned bit-packed bounds-checked codec (version 11). [docs/net.md](docs/net.md) |
| **abilities** | `abilities.[ch]` | sim | Ability table, loadouts and unlocks, status effects (stun, fear, slow, latch, defensive, marked). |
| **progress** | `progress.[ch]` | sim, world | Named places from the world data, quests, nests and eggs. |
| **character** | `character.[ch]` | sim, abilities | Saved characters in `~/.config/cretaceous/characters/<name>.ini` (`%APPDATA%\Cretaceous\characters` on Windows). Player and server only. |
| **gfx** | `render/gfx.[ch]` | raylib (window and GL loader only), GL 3.3 | Shader loading with recursive `#include`, render targets and texture helpers. |
| **view** | `render/view.[ch]` | gfx, terra | Terrain, water, atmosphere, volumetric clouds and post-processing (details below). |
| **veg** | `render/veg.[ch]` | gfx, terra | Procedural plants, impostors and instanced drawing (details below). |
| **csm** | `render/csm.[ch]`, `shaders/csm.glsl` | gfx | Cascaded sun shadow maps: plants, props and agents cast; terrain, plants, props and water receive. |
| **dino** | `render/dino.[ch]`, `shaders/dino.vs/fs` | gfx, sim | Skinned dinosaurs from `assets/dinos/*.dino` (details below). |
| **player** | `cretaceous.c` | all | Solo, host, join, server and capture modes. |
| **capture** | `capture.[ch]` | view, sim | Scripted camera paths rendered frame by frame and piped to ffmpeg (`capture --path`). |
| **fx** | `render/fx.[ch]`, `shaders/fx.vs/fs` | view, dino, hud, sim | Combat feedback from the sim's hit log (details below). Presentation only. |
| **audio** | `audio.[ch]` | raylib audio, sim | Ambience beds mixed from the world around the listener, positional footsteps, calls, attacks, hits, eating, drinking and deaths, and menu sounds (details below). Player only; the trainer never links it. |
| **menu** | `menu.[ch]` | hud | Title screen over a flyover of the island, settings (quality, render scale, display, interface size, mouse, camera shake, hit-stop, volumes, key list) saved to `~/.config/cretaceous/settings.ini` (`%APPDATA%\Cretaceous\settings.ini` on Windows). |
| **hud** | `hud.[ch]` | raylib, sim | Health, stamina, hunger and thirst bars, growth, injuries, compass, minimap and island map, group panel, group-coloured nameplates, rally marker, ability bar with cooldowns and status chips, quest tracker and markers, death screen, pause menu and credits (read from `assets/dinos/CREDITS.md`). Laid out for 1080 lines and scaled to the window height (1.0 at 1080p), times the Interface size setting (50% to 200%; above 100% capped so the HUD keeps 600 layout lines and the menu screens 880); fonts re-rasterise at larger sizes above 1080p. |
| **binding** | `cretaceous.h` | sim | PufferLib env: every agent is a learner. |

The terra bake pipeline:
- tectonic plates and uplift;
- multigrid stream-power erosion with a channel threshold, sediment deposition, implicit diffusion and mass-conserving talus toward 20° (soft rock) to 37° (hard beds), then a hard slope cap a little above it (about 34° soft, 39° typical, 51° on the hardest beds). The relaxation alone could not keep up with uplift and incision on the 8 m cells late in the evolution and left 55–60° spires ("mouse-pulled" peaks); with the cap, summits have broad bases, ridgelines and eroded shoulders (version 20: land steeper than 45° fell from 23% to 4%, the mean half-height radius of the main peaks grew from 280 m to 620 m, the highest summit is about 630 m instead of a 1140 m spire);
- orogens 0.065–0.1 of the map wide, segmented along their length; hotspot volcanoes as broad warped shields with long concave flanks and a summit crater;
- droplet erosion and lakes from a priority flood;
- coastal flats: on about half the coastline, land near the sea is lowered onto a gentle beach profile (35–110 m wide) and the seabed offshore is raised into a shallow shelf;
- rivers carved along flow;
- surface materials and vegetation from moisture, temperature, slope and curvature.

The view module covers:
- quadtree-LOD terrain with a B-spline-prefiltered height texture, so the smooth surface passes through the baked samples; the sim stands animals on the same surface (`cret_height` samples the world's shared B-spline coefficients), so drawn and simulated ground agree;
- height-blended triplanar materials and horizon-angle terrain shadows;
- precomputed sky (transmittance and sky-view LUTs);
- volumetric clouds with a weather map, checkerboarded half-res marching and temporal accumulation;
- moonlit nights (see Night below), with a moon disk, twinkling stars and a faint Milky Way;
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

**Trees and tall plants** (`render/veg_gen.h`, built on the CPU at startup in parallel, well under a second): every tall kind has 8 variants, 4 species (`variant & 3`) times 2 seeds, plus per-instance scale, hue, yaw and a lean of up to about 2.5 degrees.
Each plant grows from a skeleton of tapered tubes (parallel-transported frames, continuous bark uv, flared joints). Foliage hangs on it as many small alpha-tested cards and leaflets, oriented in 3D around their twig or rachis, so a crown holds up from any side and from below.
- conifer: Norfolk-like araucaria (whorled tiers of slim branches clothed in flat pinnate sprays, alternate, swept forward), podocarp (excurrent, strap-leaf clusters), umbrella araucaria (below) and metasequoia (buttressed, spiralled branches, twigs of hanging sprays);
- broadleaf: magnolia, laurel, multi-stemmed katsura and a drooping understory tree, forked limbs inside a lumpy crown envelope with twigs carrying photo leaf clusters;
- cycads (rachis tube and keeled leaflet pairs, a skirt of dead fronds) and Williamsonia (forked, a cone at each crown);
- tree ferns (bipinnate arching fronds, croziers, a dead skirt) and palms (costapalmate fans of folded segments, or feather palms);
- shrubs (a small branching crown) and horsetail colonies (jointed stalks with needle whorls and spore cones).
A coarse leaf-area grid over each mesh bakes every vertex's sky visibility (AO and crown depth) and bends foliage normals outward.
Leaf clusters, leaflets, segments and cords are new atlas layers (`L_CL_*`, `L_LEAFLET`, `L_PSEG`, `L_ROPE`, `L_FPINNA`, `L_SCALES`, `L_HSTEM` in `render/veg.c`), cut from the photo sprites or painted when those are missing.
The full mesh (with twigs, which cast no sun shadows) is drawn within `tree_lod_dist` (32 m on high, a little less for the small tall kinds), then the reduced mesh (no twigs, merged leaf cards, fewer leaflets) with a dithered crossfade, then the impostors past `CRET_VEG_NEAR`. The impostors are baked from the full mesh at startup (albedo 256², normal and AO 128²).
The full and reduced meshes of a variant are the same plant with the same skeleton: every branch path is grown once at full resolution on both meshes and the reduced one only draws it through every other ring (`path_coarse`), its leaf cards are every third of the full mesh's cards at the same points and rolls (merged larger, `leaf_cards`), and anything whose number of random draws depends on the detail level runs on a forked stream, so the crossfade only trades detail and no branch moves. The reduced meshes used to be grown with fewer, longer steps, which put every limb somewhere else and showed as branches jumping at the switch.
`cret_veg_trunk_radius(kind, variant, scale)` gives the base trunk radius from the same seeds without building a mesh (trunk colliders, `cret_veg_cast_trunks`).

**Tall grass** (`VEG_GRASS` in `render/veg.c`): clumps of arching ribbons textured with blade-photo strips (atlas layers `L_GRASS`, `L_SEDGE`, `L_REED`, pinched at the base into a tuft).
- Variants: meadow grass (about 0.5–0.9 m), tall grass (0.8–1.3 m), a sedge fountain, and reeds with a sedge skirt (1.1–1.6 m) that may stand in 15 cm of water.
- Placement is regenerated around the camera on a 0.8 m lattice. Density follows open living ground (fern prairie, soil, mud, some moss and litter; never rock, scree or sand), thins under a closed canopy, and is highest at lake and river edges, where sedge and reeds take over. A ~6 m tussock noise inside ~40 m meadow patches makes it clumpy.
- Drawn out to half the ground-cover range (45 m on high, 36 m medium, 27 m low), full mesh within 15 m (scaled by preset) and a 16-triangle mesh beyond. It shrinks a little and dithers out over the last 30%. Casts into the first shadow cascade within 16 m.

**Tree wind** (`shaders/veg.vs`, trees have stiffness 0): the trunk bows downwind with the gusts, more toward its top; each limb or frond sways on its own phase (all its twigs with it); sub-branches, twigs and leaflets sway faster from where they leave their limb; and leaves flutter about their own plane. The weights and phases are per vertex (`VVert.w` in `render/veg_gen.h`).

**Bending** (`shaders/veg.vs`): every ground-cover instance carries a stiffness (grass and sward 1, ferns 0.75, horsetails 0.6, scrub 0.3; trees 0) and its height.
Each point swings about the ground below it, more toward the tip, and its normal turns with it.
- Wind: gust bands roll downwind at about 7 m/s and waver across the field, so a meadow leans and sways in waves; wind is stronger under an overcast sky.
- Animals: the trample map (`render/trample.h`, `shaders/trample.fs`) is a 256² RGBA16F target at 0.4 m per texel (102 m) that scrolls with the camera.
  Each frame one pass stamps the contact-shadow bodies (the 16 nearest animals within 45 m), pushing the cover outward from each spine; bigger bodies push harder (raptor 0.8, stegosaur about 1.1, tyrannosaur 1.25, where 1 lays a blade about 75° over).
  Blades follow within about 0.1 s, spring part of the way back over about half a second, and a trail stays 70% flattened, fading over about 4 s.

The dino module covers:
- GPU skinning: joint matrices for every agent in one float texture, one instanced draw per species, LOD and material, also into the shadow cascades;
- three LODs by distance relative to body length;
- locomotion clips blended and phase-synced by ground speed, with playback rate matched to stride; jump, eat, drink, call and attack crossfade in;
- the body pitches and rolls with the ground under its hips (within species limits: big bipeds keep their backs nearly level, quadrupeds follow the line from hind to front feet) and leans into turns;
- foot IK near the camera (LOD 0 and 1): legs found by joint name (hip, knee, ankle); each foot keeps its animated height over the real ground under it (terrain, or a rock or log's top through `cret_ground_at`), the pelvis drops so the lowest foot reaches (no further than the highest foot can fold), knees stay in their bend plane, and planted feet tilt about their contact toward the ground normal; fades out in the air, swimming and in death;
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

Combat: attacks aim where the camera looks, within the species' neck arc (the stegosaur's tail behind it). They keep 70 to 92% of the gait until the blow lands and never root the animal. A bite lands on one animal, the body nearest the jaws; tail swipes and slashes sweep everything in their arc. Carnivores eat from a carcass at the jaws or lying beside the front half of the body; the HUD says when E will eat, or how far the nearest carcass is.
The secondary attack (right mouse) is a tail swipe with mass-scaled knockback for the tyrannosaur, pachycephalosaur and sauropelta, a wide tail sweep for the stegosaur, and a quick pass-through slash or kick for the raptor and gallimimus.
The head turns toward the aim. On the move, attack clips play over the neck and head only, and tail swipes are procedural (`render/dino.c`).

These pieces could stand alone as open-source repos: **terra** (terrain baker), **sky + clouds** (the view atmosphere passes) and **veg** (procedural plants and impostors).
Once a second game uses them, `gfx` would become the shared base library.

## Movable props

Boulders, stones of 28 cm radius or more, logs and driftwood are Box3D dynamic bodies (`dynprops.c`); smaller stones, roots, coast and cliff pieces stay static. Mass is volume × density (rock 2400 kg/m³, wood 420 kg/m³): on the test island boulders weigh 0.1–18 t (mean 1.8 t), stones 20–270 kg, logs 150–1740 kg, driftwood 70–510 kg. Props are seated on their footprint (the rim of the mesh's open bottom sits on the ground; knife edges and overhangs are refused), and a moved prop draws a cap over that bottom.

- **Pushing**: walking into a prop pushes it with up to strength × animal mass × g, scaled by how hard the animal leans in (0.4 standing to 1.3 sprinting), as an impulse at the contact point (boulders roll, logs turn), never faster than the animal moves into it, plus the momentum of the collision when running into it. Strength by species: pachycephalosaur and sauropelta 1.3, stegosaur 1.2, tyrannosaur 1.1, raptor 0.8, gallimimus 0.5. A stegosaur or tyrannosaur shoves a 2 t boulder about 4 m in 3 s; a raptor does not move it but rolls a 150 kg log and kicks stones. A prop under 8% of the animal's mass, or below its knee and under three quarters of its mass, does not block it: the animal steps over it and kicks it aside.
- **Crushing**: a prop closing on an animal faster than 2.5 m/s (net of the animal moving away) takes 0.5 μ v² / (victim mass × 25 J/kg) of its max health (μ the reduced mass), times (1 − armour / 2), ignored under 2%; a third or more breaks a bone. Resting and pushing contact never hurt. The animal is knocked along. The last animal to push the prop in the last 5 s gets the hit (`CRET_HIT_CRUSH`) and the kill; otherwise the death is the environment's.
- **Cost**: the physics world steps at 15 Hz (4 substeps) and only while a prop is awake. At most 32 are awake (the slowest beyond that sleep) and none for more than 20 s; at most 256 moved props are tracked. Props at rest far from every animal drop their body until one comes near. Steps are deterministic (`test_dyn_determinism`).
- Observations and actions are unchanged (`CRET_CONTRACT_VERSION` not bumped): agents see props as before.
- `capture DIR --path boulder` finds a big boulder above a clear slope and lets it go above a raptor.

## Combat feedback

`render/fx.c` turns the sim's hit log (`sim.h`, `CretHit`: tick, attacker, victim, kind, damage) into presentation. It never changes the sim, so rules, rewards and observations are untouched. Net clients get the hits near them in each snapshot (protocol v10, docs/net.md), so hosts and clients see the same feedback.
- **Blows you land**: a hit-stop of 40-80 ms by damage (the local view holds still, the sim runs on), a camera shake by damage and the two animals' mass, a hit sound (thump for butts and tail blows, crunch for bites and slashes, both on heavy hits), a hit marker on the reticle (red and longer for a kill) and the target's health bar for 4 s, with the lost part draining after it.
- **Blows you take**: a damage arc on a ring around the reticle pointing at the attacker (fades over 1.4 s, up to 6 at once), a red edge vignette pulse and a camera kick away from the blow, all scaled by damage.
- **Every hit within 140 m**: blood droplets from the strike point (head, tail tip or thorns; 6-32 by damage), ground decals draped on the terrain that fade over the last half of about 30 s (64 at most, not under water), a flinch on the victim (head and neck snap away from the blow, the body leans with it, scaled by damage and the mass ratio), and bleeding animals drip, each drop staining the ground.
- Biters snap the head forward at the strike time, and tail swipes leave a faint arc trail for 0.18 s.
- The HUD shows cooldown radials on the primary and secondary attacks next to abilities 1-3.
- Settings has Camera shake and Hit-stop, each Off or 25-100%.

`capture --shot 'dino_feel_*'` renders a bite with the game HUD, a tyrannosaur's bite on the player and a tail swipe, and prints the effect counts (`CRET_FX_DEBUG=1` adds the effect joints' world positions).

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

5. **Sink, don't cut.** Beside a river the extended level can stand above a lower dry bank. Cutting the sheet at the mask's 0.5 line left straight, cell-shaped water edges, so the sheet instead sinks below the ground as the mask fades from 0.5 to 0.2 (`water.vs`, and the same in `water.fs`), and the terrain depth test cuts a natural edge.

The shader takes depth (surface level minus ground height) for its shore foam and refraction fallback.
It takes the water velocity (below) for its river normals, and the level's own slope (central differences 2 m apart), so a steep creek is shaded as a tilted surface rather than as level water seen at a grazing angle.
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

`water_flow_speed(n, kind, surface, cell, &params, out)` writes the matching surface speed (0..1, `t_misc.z`): `speed_min` (0.25) plus `speed_slope` (6) times the water surface slope, clamped to 1, then a box blur of `speed_radius` (3 cells) run twice over the water and the same dilation.
The speed has to be as smooth as the direction: a flow map advected by a speed that steps at a cell edge shears its ripples into hard bands along the step.

`water.fs` reads the field from `t_misc.xy`. It advects ripples with a two-phase flow map: two copies half a cycle apart, each reset while it is invisible, so turning or diverging flow never tears. It blends river ripples into still-water chop by |v|, and fades the silt colour and the end of the breakers closer in, so a mouth has no boundary.
Every layer that moves with the current (ripples, the fine near-field chop, rapids foam) goes through the same two phases. Scrolling a pattern by `flow * time` instead shears it without bound wherever the flow varies, and after a few minutes of play compresses it into noise: zebra bands on fast water and sparkle-filled cells where a steep reach met a slow one.
The drift speed is `flow_speed × 2.5 × speed`, softly capped at `max_speed` (tanh), so a steep creek runs at about 2 m/s instead of 14.
Fast water is glossy: its ripple slopes along the current are flattened (`fast_gloss`), so glints stretch into smooth streaks down the current. This works on the slopes, not on the coordinates, so it cannot shear either.
Sun glints keep their far-field look (the long streaks and arcs on the far bank). Within about 60 m the lobe widens by how fast the normal changes across a pixel (specular anti-aliasing) and each glint's peak is capped (`sparkle_peak`), so near sparkles stay small points rather than white discs under bloom.
Ripples never show the noise texture's tile. Each ripple layer sums two octaves at incommensurate scales (x2.17), each rotated by its own angle and rotated back into world space. The two flow-map phases differ in scale and angle too, and a broad gust pattern varies the ripple strength. Otherwise sharp sun glints map the repeat out as a regular grid of sparkles.
The build takes about 0.2 s for 2048². `CRET_WATER_FLOW=0` shows the raw D8 directions for comparison.

**Tuning the look.** `config/water.ini` holds the look: flow speed and its cap, ripple size and strength, fast-water gloss, rapids foam, near sparkle brightness, size and peak, the clear and silty water colours, turbidity and clarity.
Edit it while the game runs and press F5, which rereads it with the shaders. Missing keys keep their defaults, unknown keys are reported, and `CRET_WATER_INI=path` uses another file.
`render/water_look.h` is the single-header parser and the defaults, so the water pieces (`water_flow.h`, `water_look.h`, `water.vs`, `water.fs`) can leave together.

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

**Foliage colour** comes from the atlas, not the lighting. Photo leaves are evenly lit and pale, so `grade_layer` (`render/veg.c`) grades every leaf layer once at startup:
- it stretches the leaf-to-leaf value contrast around the layer's mean;
- it darkens toward the rachis and the frond base (deep blue-green) and warms the tips (yellow-green);
- it browns a few tips in patches;
- it settles the layer's mean a little deeper than its photo target.
The impostors are baked from the graded atlas. The terrain's fern sward stretches its photo detail the same way (`RECOLOR_CONTRAST` in `terrain.fs`).
Bark photos are scaled to a species tone, and values above the photo's mean are compressed and pulled toward its hue, so pale flakes and lichen (the metasequoia photo's white flecks, used for every conifer) stay lighter bark instead of birch-like white blotches.

**Umbrella araucaria** (`VEG_CONIFER` variants 2 and 6, Araucaria araucana-like): a bare trunk with dead branch stubs to about 62% of its height under a dome. Branch length follows a quarter circle up the crown (4.6 m at the bottom whorl, 1.5 m at the top), branches run out nearly level and turn their tips up, woody near the trunk and then a slim dark scale-leaf tube under a sleeve of crossed rope cards (the sawtooth outline of the scale leaves, which a smooth tube could not give) with rope-cluster branchlets curling up and out on alternate sides, and the whorls continue to the leader under a cap of rising rope clusters. The branches used to be 13 cm scale-textured tubes with sparse branchlets and read as rubber hose; the Norfolk's were thicker still (16 cm at the trunk, tapering slowly) with sparse cords, which is what looked like play-doh ropes. Both are now 4 to 9 cm poles, pencil-thin at the tip.

**Contact shadows** (`csm.glsl` `contact_occlusion`, `cret_csm_set_contacts`): the 16 nearest animals within 45 m are passed as ellipses (position, yaw, half length, half width, height).
Terrain, plants and props darken their sky light under and around each animal, so animals sit in the grass under an overcast sky and at night too. The sun shadow maps carry the direct shadow, and the contact shadows work with them off.

## Night

Night is shot day-for-night. Once the sun is well below the horizon, the moon (opposite the sun) becomes the key light, so shadows, clouds and water keep working.
The frame is lit like day and then graded in `post.fs`:
- **Cool key light.** At night the transmittance LUT carries `MOON_TINT` (`common.glsl`), so the moonlight, the moon disk and the moonlit sky are blue-white.
- **More sky light than sky.** The sky-view LUT is scaled by `NIGHT_SKY_LIGHT`, so shadows, forest and the sides of animals stay readable. The dome you see keeps only `NIGHT_DOME` of that gain (`sky.fs`, and the water's sky reflection), so it stays dark and starry. Moonlit clouds are dimmed to silver-grey.
- **A light floor.** A faint starlight and airglow term in the sky-view LUT keeps ambient light during the minute after sunset before the moon clears the horizon, so dusk and dawn never go black.
- **Grade.** Exposure drops 1.5 stops at night. Rod vision (Purkinje) then shifts dark tones toward a desaturated blue, and moonlit highlights keep a little colour. The tone curve's toe softens (slope 1 at black instead of ACES's crush), so the low stops where a moonlit scene lives stay apart.

Eye adaptation still works at night: it brightens dark views by up to 3 stops over the sun-based exposure, at 60 to 85% strength.

## Developer tools

### Debug console (`debug.c`, commands in `cheats.c`)

Backtick or F12 opens it in play, host and join (raylib's own F12 screenshot also fires, writing `screenshot000.png`). Up/Down walk the history, Tab completes commands, species and words, PgUp/PgDn scroll, Esc closes. While it is open the game ignores the keyboard and mouse. Commands that change the world (marked *) work only for the host or a solo player; a client gets "not allowed". View toggles work everywhere, except that clients cannot use `labels`, `navdebug` or `near`, which show other animals' AI.

| Command | Effect |
|---|---|
| `help [command]`, `clear` | List the commands or show one command's usage; clear the log. |
| `spawn <species\|random> [dummy\|passive\|hostile\|bot] [n]` * | Spawn n animals (default one dummy) at the crosshair. A dummy stands still and only turns, a passive animal flees, a hostile one attacks you, and a bot runs the game AI. Spawned animals take over ambient bot slots. |
| `kill [target\|near\|all]` * | Kill the animal under the crosshair, every animal within 100 m, or every animal. |
| `heal` * | Restores full health, food, water and stamina, and mends bones and bleeding. |
| `god [on\|off]` * | You take no harm and never get hungry or thirsty. |
| `tp <x> <z>`, `tp nearest <species>` * | Teleport to a point, or next to the nearest animal of a species. |
| `time <0..24>`, `weather <clear\|overcast\|rain\|auto>` * | Set the hour; fix the sky or let it drift again. |
| `speed <0.1..8>`, `freeze [on\|off]` * | Set the sim time scale; pause all AI. |
| `grow <0..1>`, `species <name>` * | Set your growth; become another species where you stand. |
| `pop` | Population report: animals within 150 and 300 m, and what the director did. |
| `prop <boulder\|stone\|log> [kg]` * | Drop a movable boulder (2000 kg), stone (60 kg) or log (150 kg) at the crosshair, to push (off a slope, into someone). |
| `colliders`, `labels`, `navdebug`, `fps`, `profile`, `near` | Overlays: collision shapes (capsules, prop hulls); a label on every animal (species, AI state or actor kind, target, health); bot goals, feelers and stuck escapes; frame-time graph; GPU and CPU ms per pass; nearest animals with distance and bearing. |
| `preset <low\|medium\|high>`, `shadows`, `clouds`, `veg`, `grass`, `water`, `fog`, `wireframe` | Graphics. `fog off` recompiles the shaders without aerial haze. |

Hotkeys: F6 spawns a random dummy and F7 a random passive animal 8 m ahead; F8 toggles labels and F9 colliders. F1 lists them. `CRET_CONSOLE="god; spawn trex hostile; labels"` runs commands at start.

### Gallery: `play gallery`

A flat, neutral 1 km plain (baked once into the world cache as seed 0x6A11E7) with the real sky, sun, shadows, LODs and materials. Rows, front to back:

- **Trees:** every tall kind and variant (conifer, broadleaf, cycad, tree fern, palm).
- **Ground cover:** fern, horsetail, shrub, moss carpet, meadow grass and wet grass, each grown from a painted 16 m patch.
- **Props:** every catalog variant at a mid size of its class.
- **Dinosaurs:** one adult of each species playing the chosen clip.

Each item has a label with its kind, variant, triangle counts, current LOD and distance.

| Key | Effect |
|---|---|
| WASD, Q/E, Shift | Fly, down/up, faster. |
| Right mouse or arrows | Look. |
| Tab, N / Shift+Tab, P | Fly to the next or previous item. |
| L | LOD: auto, full, reduced, or impostor/lowest. |
| T / G, [ / ] | Time of day ±1 h; sun angle ±10 min. |
| R | Weather: clear, overcast, rain. |
| C / V | Next or previous clip (for every dinosaur, or only the focused one). |
| Z | Slow motion: 1, 0.25, 0.1. |
| X, K | Dinosaur wireframe; skeleton. |
| H | Labels. |
| F10 | Screenshot to `$CRET_SHOT_DIR` (default `screenshots/`). |
| F2, F5, F1 | Terrain wireframe; reload shaders; help. |

`capture DIR --gallery [--size W H]` writes each row in pages without a window (`gallery_trees_01.png`, …). It also writes the dinosaurs running with skeletons, attacking in wireframe, the trees as impostors, and an overview. `CRET_GALLERY_LODS=1` adds every tree page forced to each LOD from the same camera (`_full`, `_reduced`, `_impostor`): the three should differ only in detail, never in where a branch is.

### Scenarios: `play scenario NAME`

Scripted fights on the normal island, set up in `config/scenarios/NAME.ini` (the format is in `scenario.h`). They contain only the animals they list: no ambient population and no respawns.

| Scenario | What it shows |
|---|---|
| `raptor_vs_stego` | AI raptor hunting an AI stegosaur. |
| `trex_vs_pachy` | Scripted T. rex against three hostile pachys. |
| `raptor_pack_galli` | Raptor pack against a gallimimus herd with young. |
| `hitrun_stego` | Raptor hit-and-run on a stegosaur. |
| `herd_predator` | Sauropelta herd defending against a T. rex. |
| `boulder_test` | A tyrannosaur sprints through a 2 t boulder and shoves it into a raptor (crush damage, credited to the pusher); a log lies to the side to push by hand. |

`play scenario` with no name, the title screen's Scenarios button and Esc during a scenario open the scenario menu: Resume, Restart, every scenario by title, Gallery, Quit. Its right column sets up **your fight**: your species and size (hatchling, juvenile, adult) against 1-8 of a species at a size, which fight you, run the game AI, run away or stand still; Start plays it with you in control (`you = on` in a scenario file does the same). The title screen also has a Gallery button. In play: M takes over the player, C cycles the camera (follow, side, orbit, high), F toggles a free camera, Space pauses, [ and ] change the speed, R restarts, and the console works. The summary prints when the scenario ends: damage, deaths, hits and ability uses per animal.

`capture DIR --scenario NAME` writes, without a window, `DIR/NAME.mp4` (30 fps, the scenario's camera, ffmpeg or `$CRET_FFMPEG`), `DIR/NAME/tNNN.png` every 5 s and `DIR/NAME_summary.txt`.

### Headless tools

- `make -C ocean/cretaceous scnrun && ./build/cretaceous/scnrun [NAME...] [--seed N] [--trace S]` plays scenarios without a GPU and prints their summaries (the trace also lists moved props).
- `make -C ocean/cretaceous stuckprobe && ./build/cretaceous/stuckprobe [NAME...]` plays scenarios and, whenever an animal is told to move but stands still for half a second, prints what holds it: a trunk, a prop (kind, variant, height against the ground), another animal, or nothing (then the slope).
- Scenarios can place movable props: `prop = boulder|stone|log X Z [KG] [YAW]` in `[scenario]` (up to 8).
- `make -C ocean/cretaceous popprobe && ./build/cretaceous/popprobe --minutes 20 [--restore far] [--player explore] [--pop 0]` measures how far the player is from the nearest animal over a session.

### Population director (`population.c`)

In solo, host and server play, the island holds 48 animals instead of 16. When nothing is within reach, the director moves herds and predators to 130–230 m from each player, and it places a returning character near a herd.

`CRET_POP=0` restores the old population.

## Debug knobs

| Variable | Effect |
|---|---|
| `CRET_TERRA="k=… uplift=… diffusion=… years=…"` | Overrides bake parameters (also `bake --set`). |
| `CRET_PROFILE_LOAD=1` | Prints wall and CPU ms of every startup stage (cache hits and misses included) and of the first two frames' passes. |
| `CRET_CACHE=0` | Neither reads nor writes the derived-data cache (`build/cretaceous/cache/`); everything is derived at startup. |
| `CRET_CACHE_DIR=PATH` | Uses another cache directory (an empty one gives a cold start). |
| `CRET_LOAD_THREADS=N` | Threads for startup decoding and hashing (default: the CPU count, at most 8). |
| `TERRA_NO_DETAIL`, `TERRA_NO_THERMAL`, `TERRA_NO_SLIDE`, `TERRA_NO_BEACH`, `TERRA_DEBUG` | Disables bake stages and prints statistics. |
| `TERRA_ACRIT` | Channel-initiation area in m² (default 4000). |
| `CRET_VEG_NEAR` | Mesh/impostor switch distance in metres (default 150). |
| `CRET_VEG_TREE_LOD` | Full/reduced tree mesh switch distance in metres (default 32). `CRET_VEG_LOD`: the same for ground cover (default 25). |
| `CRET_VEG_LINEUP=1` | `capture` plants the lineup of every tall kind and variant and adds its `lu_*` shots. |
| `CRET_GALLERY_LODS=1` | `capture --gallery` also writes every tree page forced to the full, reduced and impostor LOD (`_full`, `_reduced`, `_impostor`). |
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
| `CRET_GRASS_RANGE=M` | Tall-grass draw range in metres on the high preset (default 45). |
| `CRET_TRAMPLE=0` | Keeps the grass and wind but animals no longer bend it (A/B of the trample map). |
| `CRET_TRAMPLE_DUMP=file.ppm` | Every 30th frame writes the trample map (red, green: bend; blue: trail). |
| `CRET_WEATHER=0\|1\|2` | Forces clear, overcast or rain. By default the weather follows the cloud cover: above about 0.7 the sky greys over, above about 0.9 it rains (streaks, haze, wet surfaces). |
| `CRET_CLOUD_CB=0` | Marches every half-res cloud pixel each frame instead of a quarter of them (checkerboard plus history; about 1 ms cheaper on open views). |
| `CRET_SHADOW_STAGGER=0` | Renders every shadow cascade each frame (default: the two farthest alternate). |
| `CRET_FLUSH=N` | Bitmask of the mid-frame `glFlush` calls (1 after shadows, 2 after terrain; default 3). |
| `CRET_DINO_TEX=N` | Caps dinosaur texture size (for example 1024). |
| `CRET_NO_PROP_COLLIDERS=1` | Props and tree trunks stop blocking movement. |
| `CRET_NO_TREE_COLLIDERS=1` | Tree trunks stop blocking movement (props still do). |
| `CRET_NO_DINOS=1` | Draws agents as capsules instead of dinosaur models. |
| `CRET_SHOT_VARIANTS="noon:0.5:0.3,gold:0.70:0.35"` | `capture` stills: renders every selected shot once per `tag:time_of_day:cloud_cover`, as `NAME_TAG.png`. |
| `CRET_WATER_FLOW=0` | Raw per-cell D8 river directions instead of the smoothed flow field (`render/water_flow.h`). |
| `CRET_WATER_INI=path` | Water look file instead of `ocean/cretaceous/config/water.ini` (reread on F5). |
| `CRET_TIME0=S` | Starts the animation clock at S seconds, so a capture still shows water and wind as after S seconds of play. |
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
