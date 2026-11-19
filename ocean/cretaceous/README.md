# Cretaceous

A survival game where you play as a dinosaur, in the spirit of Path of Titans. It doubles as a multi-agent RL environment.
The trainer, the dedicated server, the listen host and solo play all run the same fixed-step `CretSim`.
Humans, network peers, scripted bots and policies all drive agents through `CretInput`.
Rendering never feeds back into the sim.

Status (2026-11-07): six playable species are skinned, animated models (`render/dino.c`):
Velociraptor, Stegosaurus, Tyrannosaurus, Pachycephalosaurus, Sauropelta and Gallimimus.
Survival works end to end:
- health, bleeding, hunger, thirst and stamina;
- bites and tail swings with reach, cooldown, knockback and a flinch; heavy blows knock smaller animals off their feet (see Knockdowns);
- death leaves a carcass that carnivores eat and that rots away; the animal respawns;
- small prey around people (mice, Repenomamus, lizards, birds, fish) that young predators catch with a bite (see Small prey below);
- growth from hatchling to adult changes size, speed, health and damage;
- broken bones from heavy hits and falls (slow, no sprint or jump, heal faster at rest);
- wounds that last: a hard blow wounds a leg, the body or the head, which heals over about a game day (slower hungry, worse starving); a leg wound limps, a body wound bleeds a little and tires, a head wound dulls the senses, and the wounded leave blood on their trail that predators follow (see Wounds);
- groups: packs and herds, shown on the HUD; a call (G) forms or joins a group and rallies its members;
- abilities on keys 1-3 (pounce and latch, fear roar, tail slam, charge, hunker, dodge leap and more), with stun, fear, slow and mark effects;
- server quests that pay growth and marks: drink, graze, hunt, patrol, defend, catch small prey, stay near your own kind, rest, find a new place, scavenge, raid or guard a nest, build a nest, and group quests. Each is feasible for the animal that gets it (hunts within a mass ratio, distances by growth: 70-150 m for hatchlings up to 250-500 m for adults); a new character's first quests teach the game in order (eat, drink, find your kind, ability 1, a new place, a nest) and are kept in its save;
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

The wildlife is an ecosystem (`docs/ecosystem.md`): energy scales with body mass (a fat store, a digestion pause after a big meal), plants are eaten down and grow back, and every animal knows only what it has seen, heard or smelled. Bots choose what to do by scoring drives (flee, drink, graze, hunt, scavenge, rest, herd, provision young, nest, guard, explore) by urgency and chance.
Herbivores graze good ground and move on when it is eaten down, look up now and then (less often in a bigger herd), and run from a predator coming inside their flight distance, which shrinks with hunger and cover, keeping to dry ground (along a shore rather than into deep water, where nothing can bite and a chase used to stall) and trotting on until 45 m clear; a fleeing animal alarms its herd. Stegosaurs and sauropeltas stand and fight; parents stand between a predator and their young.
Predators hunt by arithmetic: the chance to catch (both sprints and stamina, surprise), to kill, and the meal against its cost and danger; a hunter stalks while the prey's head is down, freezes while it looks, charges from close and gives up when the gap stops closing. Packs join and flank. After a kill they eat, guard it, and parents carry a piece home to the young at the nest. Each species keeps its hours (raptors at dawn and dusk, tyrannosaurs into the night). `tools/ecoprobe.c` measures it all.
Bots take time to react. They notice a new threat or a hit after a delay that grows with their mass: about 0.2 s for a raptor, 0.4 s for a pachycephalosaur or gallimimus, 0.55 s for a sauropelta and 0.7-0.75 s for a stegosaur or tyrannosaur. Their head (a stegosaur's tail) turns to aim at a limited speed, from 6.5 rad/s for a raptor down to 1.4 rad/s for a tyrannosaur, also during an attack's wind-up. A quick animal can bite a big one's flank and get clear before it turns.
A hit leaves a bot alert to its attacker for 20-60 s. A herbivore keeps defending, or keeps away from the attacker with its herd, instead of going back to grazing beside it. A predator turns on an attacker it can take and chases it, and backs off from one it cannot. The alert runs down faster while the attacker is far off and ends once it is 120 m away or dead.
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
./build/cretaceous/play server --snapshot-hz 20   # headless; host and server send 15 snapshots a second by default (20 or 30: docs/net.md, Snapshot rate)
./build/cretaceous/play server --checks log --telemetry day1.ctl   # server checks and the per-player telemetry log; --aoi off sends everyone everything (docs/anticheat.md)
```

```bash
./build/cretaceous/play join 192.168.1.20   # client (--species stego asks the server to spawn you as one)
```

Joining loads the server's world on the client: from the cache in seconds, or a first bake of about 45 s for a world it has never seen. The loading screen shows the stage and its progress, and the connection stays up meanwhile: the client tells the server how far it is, the server holds its animal and sends it nothing until it is ready, and gives up after 5 minutes with a message on both sides (docs/net.md, Join and loading). Nobody else on the server waits for a joining player.

```bash
./build/cretaceous/play play --policy carnivores=checkpoints/cretaceous/<run>/<step>.bin   # the predators run a trained policy
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

### Play against a trained policy

`--policy [WHO=]CHECKPOINT[:NET:HIDDEN:LAYERS]` on `play`, `host` or `server` lets a checkpoint the trainer wrote (`checkpoints/cretaceous/<run>/<step>.bin`) drive the animals WHO names instead of the bots: `all` (the default), `herbivores`, `carnivores`, a species (`raptor`, `stego`, `trex`, `pachy`, `sauropelta`, `galli`) or `slotN`.
- People are never driven: your animal and network players keep their own input. With `--bot` the policy also plays your animal when WHO includes it (`--bot --policy slot0=FILE` to watch it).
- Either encoder loads (`cretaceous_net` 0 or 1); the encoder, hidden size and layer count are found from the file's size, or named with `:NET:HIDDEN:LAYERS`. A checkpoint of another contract or size is refused at startup with both float counts.
- Actions are sampled as in training; `--policy-greedy` takes the most likely action instead.
- It runs on the CPU (`policy_cpu.c`, docs/encoder.md "CPU inference"): the island's 48 animals at 256 x 2 cost about 1-2 ms of the 33 ms tick on 4 cores, at 1024 x 2 about 6-8 ms.

What to look at: `./build/cretaceous/play play --policy carnivores=FILE` and stay near raptors and tyrannosaurs; with an untrained or zero checkpoint they wander and twitch, which is the expected look of a policy that has learned nothing. The `labels` overlay's AI state comes from `bots.c`, which does not run for policy slots, so it stays frozen on them.
`cret_eval --policy all=bot --policy carnivores=cpu:FILE` measures the same thing headless (docs/testing.md).

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

### Web build

A single-player build for the browser: WebAssembly and WebGL2, built with Emscripten. It plays solo (Play) and "Play a fight". A desktop with a hardware GPU starts on the high graphics preset at full resolution and steps down by itself if the frame rate stays under 30 fps; phones, tablets and software renderers start on the low preset (docs/web/README.md, "First-visit quality"). Settings and saved characters are kept in the browser's storage across visits. Hosting and joining are shown on the title screen but disabled; the RL trainer is not part of it.
On phones and tablets it has touch controls drawn by the game (a stick, camera drag, attack, jump, eat, call and ability buttons, menus by tap), a page that neither scrolls nor zooms and follows rotation, and a small-memory texture profile; `docs/web/README.md` describes them, the memory measurements and what world streaming would take.
It is a directory of static files served from relative paths (no server code, no COOP/COEP headers, every file at most 14 MiB), so it can be hosted as a claude.ai Artifact or on any static host.

Needs, next to the repository root (the same layout as the native build):
- emsdk (`git clone https://github.com/emscripten-core/emsdk ~/emsdk && ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest`; built and tested with Emscripten 7.0.0 and 6.0.2);
- `raylib-5.5_webassembly/` (the raylib 5.5 release `raylib-5.5_webassembly.zip`, unpacked in the repository root);
- the Box3D fork in `../box3d`; the script builds its wasm library once into `../box3d/build-web`, apart from the native `../box3d/build`;
- python3 with numpy and Pillow for packaging.

```bash
EMSDK=~/emsdk ocean/cretaceous/tools/package_web.sh --assets ocean/cretaceous/assets   # build + pack -> build/cretaceous/web
```

```bash
python3 -m http.server -d build/cretaceous/web 8000                                     # then open http://localhost:8000/
```

`tools/build_web.sh [OUT]` only builds (`cretaceous.js`, the gzipped `cretaceous.wasm.bin`, `index.html`, `loader.js`); `tools/package_web.sh` builds and then packs the data with `tools/web_pack.py` (`--no-build` packs only, `--world FILE` picks the baked island, `--assets DIR` any directory laid out like `assets/`). Packaging reads the baked island from `build/cretaceous/worlds/`, so run the native game once after a terra change.
Page parameters: `?mobile` forces the small-memory profile (docs/web/README.md), `?play` goes straight into solo play, `?species=trex`, `?fight` opens the scenario menu, `?preset=medium` (fixes the preset: no automatic stepping), `?profile` prints the load stages to the browser console, `?env=CRET_SHADOWS=0,CRET_GRASS=0` sets any of the debug knobs below, `?w=960&h=540` fixes the canvas size.

Tests (no GPU needed):

```bash
python3 -I ocean/cretaceous/tools/web_shader_check.py   # every shader compiles as GLSL ES 3.00 (glslangValidator)
```

```bash
node ocean/cretaceous/tools/web_smoke.js --fight        # headless Chromium on SwiftShader: title, solo play, a fight, no GL errors, memory (about 30 min)
```

```bash
node ocean/cretaceous/tools/web_smoke.js --phone        # a phone held sideways: taps into play, stick and camera at once, rotation, menu by tap, memory (about 15 min)
```

```bash
node ocean/cretaceous/tools/web_smoke.js --short-press  # clicks and keys of 10-30 ms on the title and in play: each counts once, in order (about 20 min)
```

```bash
node ocean/cretaceous/tools/web_smoke.js --settings     # first-visit quality (desktop, phone, software renderer), settings and a character kept across a reload, menu arrows, Alt walk, browser keys (about 40 min)
```

How it differs from the native build (all behind `#ifdef __EMSCRIPTEN__`; the sim, observations and rewards are untouched):
- **Threads.** Static hosting has no SharedArrayBuffer, so it is single-threaded: OpenMP is not enabled (its pragmas are ignored), and the world load thread and the startup decoders fall back to running inline, as they already do when a thread cannot start. The main loops are unchanged: with `-sASYNCIFY`, `WindowShouldClose` (wrapped in `cretaceous.c`) hands control to the browser at the end of each frame until the next animation frame and until a WebGL2 fence shows the GPU has finished that frame. raylib's own version sleeps 12 ms on a timer, which let a slow GPU fall behind until the tab ran out of memory.
- **Networking.** `net_web.c` replaces `net.c`, `netclient.c` and `netsim.c`; hosting and joining fail at once and the title greys them out.
- **GL.** `render/gfx.h` includes `render/gl_web.h` (the GLES 3 headers) instead of glad. The renderer already avoided desktop-only GL apart from a few calls, which the header maps or drops: timer queries read 0 ms, wireframe is ignored, `GL_DEPTH_CLAMP` is dropped (so a shadow caster in front of a cascade's box can be clipped), `glClipControl` is not used (the depth path for GL without `ARB_clip_control`), `GL_FRAMEBUFFER_SRGB` is implicit in ES. Ground materials are 1K (`CRET_MAT_SIZE=1024`), BC3 where the browser has `WEBGL_compressed_texture_s3tc` and its sRGB variant (desktop browsers); phones and tablets get the small-memory profile, RGBA8 at smaller sizes (docs/web/README.md).
- **Shaders.** One source for both builds: when `gfx.c` loads a shader on web it replaces the `#version 330` line with `#version 300 es` and `shaders/web_es.glsl` (default precisions for floats and the 3D, array and shadow samplers). The shaders needed no other change; `tools/web_shader_check.py` keeps it that way.
- **Window.** The page sizes the canvas; Scenarios and Gallery reuse the title's window rather than closing and reopening it. The first visit's preset depends on the device (above); a choice in Settings wins and is kept. Display in Settings enters the page's fullscreen, where the keyboard is locked so Ctrl combinations reach the game; Window size follows the browser window.
- **Keys.** Walk is Left Alt instead of Left Ctrl: the browser handles Ctrl+W (walk forward: close the tab) and Ctrl+T and Ctrl+N before any page can, and Ctrl+S, D and R are its shortcuts too. Ctrl still walks. The page keeps F1, F3, F6, F7, F10, Alt and Alt combinations from the browser, and in play also F5 and the Ctrl combinations it may keep; leaving a game in progress asks first (docs/web/README.md, "Keys the browser takes").
- **Saves.** Settings and characters are written to the in-memory file system as natively and copied at once into `localStorage`, then put back before the next visit's `main` runs; the character is also saved when the page is hidden or left. Private windows keep them for the session; with storage blocked the game forgets on reload, as before.
- **Files.** `web/loader.js` downloads `data/manifest.json` and the bundles, inflates them with `DecompressionStream`, writes every file into the in-memory file system at the path the native game opens, and only then lets `main` run. It also skips raylib 5.5's per-flush `glEnableVertexAttribArray(-1)` (its ES2 batch renderer enables a normal attribute its default shader lacks), which WebGL would log on every frame.

What ships and what is rebuilt in the browser (2048² island):

| Data | Native | Web |
|---|---|---|
| World (`worlds/s1_n2048_*.terra`) | 177 MB, baked in 28 s on 4 threads | Ships. Rows delta coded and split into byte planes, then gzip: 26.8 MB. Rain, soil and temperature, which only the bake reads, are zeroed (`web_pack.py` checks the sources for readers first; `--keep-all-fields` keeps them: 45 MB). Loads in 0.7 s |
| Cloud noise volumes (`cloud_*.bin`) | 9 MB, 5.6 s of CPU | Ship (8.7 MB gzip; noise does not compress) |
| Terrain textures (height, horizons, normals, splats, water, vegmix) | `cache/`, 143 MB | Rebuilt: 6.6 s |
| Ground materials | `cache/`, 89 MB (BC3) | Rebuilt from the 1K JPGs: 1.4 s |
| Plant atlas, plant meshes, impostors | `cache/`, 46 MB | Rebuilt from `assets/foliage`: 3.4 s |
| Assets | gitignored | Ship as they are, except the ground textures (re-encoded at 1K). Credits and licences always ship. Missing ones fall back as natively: grey ground, painted plant layers, no props, capsules for dinosaurs, no sound, raylib's font |

The derived-data cache is off on web (`CRET_CACHE=0`): the in-memory file system is gone on reload, so writing it would only hold a few hundred MB of RAM.
Download with this sandbox's assets (textures and foliage; no props, dinosaurs or audio): 56.8 MiB in 11 files, the largest 14.0 MiB. Measured in headless Chromium on 4 CPU cores from a local server, startup takes about 12.5 s of CPU before the first title frame (download and unpacking 1.7 s, world 0.7 s, terrain 6.6 s, ground materials 1.4 s, plants 3.4 s), and the title is on screen at about 24 s, the rest being SwiftShader drawing the first frames. Over a network add the download (57 MB is about 9 s at 50 Mbit/s). The owner's dinosaurs, props and audio add their own size.
On SwiftShader (software WebGL2, low preset) a frame takes 5 to 10 s on the title and 10 to 28 s in play, mostly vegetation; a real GPU is needed to play. The CPU side of a frame (the game's thread, with its WebGL calls) is about 5.5 ms on the title and 5.5 to 7 ms in play at 1280x720 (docs/web/README.md, "Frame time": what it costs, what changed, the knobs).

Limits and what is left:
- No timer queries (`CRET_GPU_PROFILE` reads 0 GPU ms; its CPU ms work, docs/web/README.md "Frame time"), no wireframe (F2), no depth clamp for shadow casters.
- The mouse uses the browser's pointer lock; untested on real hardware here, as is sound (no audio assets in the sandbox). Clicks and keys shorter than a frame still count once each, and the Esc a browser keeps when it ends the lock still opens the menu (docs/web/README.md, Keyboard and mouse).
- Memory: the island's file is deleted from the in-memory file system once loaded, the bake-only fields are not loaded, and terrain cache items are not written on web. A phone held sideways (emulated) went from a 1.19 GB renderer process and a 0.74 GB GPU process to 0.70 GB and 0.51 GB (docs/web/README.md, Memory). The wasm heap stays at 443 MB after a 352 MB load peak; the world is held whole (no streaming).
- On web the first frame of a scenario rebuilds the view (about 12 s), as the title's view is closed first.
- Quit ends the program; the page has to be reloaded.

Camera-path videos for reviewing motion (silent, windowless; needs `ffmpeg`). Paths: `flyover`, `forest_walk`, `tp_run`, `dusk_night`, `rain_in`, `turn`, `grass_meadow`, `grass_river`, `grass_push` (a raptor and a stegosaur walking through tall grass), `grass_trail` (the same from high behind, showing the lane they leave), or `all`:

```bash
./build/cretaceous/play capture /mnt/d/puffer_media/cretaceous/v1 --path all --size 1280 720
```

Controls:

| Key | Action |
|---|---|
| W A S D | Move relative to the camera at a trot; hold Ctrl to walk (Alt in the browser build) |
| Shift | Sprint (uses stamina) |
| Space | Jump (raptor only) |
| E (hold) | Eat, or drink when facing water |
| G | Call: form or join a group with nearby animals of your species, rally your group |
| M | Island map (the corner minimap shows 900 m around you) |
| Left mouse | Attack, aimed where the camera looks within the neck arc (bite; the stegosaur thrusts its tail behind it, the pachycephalosaur headbutts). Standing still, the body turns toward a blow aimed past the neck arc (the tail toward its aim). Never roots you; a click up to 0.3 s early is queued (a queued bite lapses if nothing is in front when it comes due) |
| Right mouse | Secondary attack: tail swipe (tyrannosaur, pachycephalosaur, sauropelta; strong knockback), wide tail sweep (stegosaur), slash or kick in passing at a run (raptor, gallimimus) |
| 1 2 3 | Abilities (slot 3 is bought with marks in the Esc menu) |
| N (hold) | Build a nest (a fed adult); it rises from the ground over the 5 s hold |
| V (hold) | Carnivores: sniff. You creep (no sprint) and see scent: wisps along the trails of prey and other animals that the wind brings you, carrion plumes, and a soft pulse on the nearest prey and carcass, through plants. A panel lists what you smell and where the wind comes from (below, "Hunting") |
| B | Carnivores: pick up the food at your mouth (a carcass up to a quarter of your mass, the rest of a catch, an egg from another group's nest) or put it down. Carrying slows you by the load and you cannot bite, eat or drink; a hard blow, a stun or death makes you drop it |
| Esc | Menu: resume, species, respawn, ability 3, settings, credits, quit (solo play pauses; clients ask the server) |
| Touch screen | A stick (left, pushed to the rim sprints), drag elsewhere to look, buttons for attack, secondary attack, jump, eat, call, abilities 1-3, the menu and (carnivores) Sniff, held; menus by tap. Shown after the first touch, hidden by a key or a click (docs/web/README.md) |
| Enter | Respawn now, when dead (clients too) |
| R | Autorun |
| Mouse, wheel | Look and zoom (0.6x to 2.5x of a close over-the-back view); the cursor is captured in play, Tab or a menu frees it. The camera never looks top-down (except down a steep hill), its boom stops short of rocks, logs and trunks, and the lens rides over terrain and water. `CRET_MOUSE_RAW=0` turns raw mouse motion off, `CRET_MOUSE_DEBUG=1` prints the mouse deltas |
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

Training (contract version 6, `sim.h`):
- Observations (contract 6, 246 floats): heading, velocity, position, terrain, water, time of day; species (one-hot over 6), alive, health, hunger, thirst, growth, bleeding, attack ready, forage underfoot, nearest carcass, hit flinch, broken bone, group size, rally point; a 7x7 terrain and water patch; the 6 nearest animals with their diet, relative size, health, same-group and same-species flags.
  Contract 4 appends 46 floats: status effects, ability cooldowns and ready flags, the quest direction and progress, the own nest, and per neighbour stunned, afraid, latched and marked (`docs/progression.md`).
  Contract 6 appends 3 floats: the animal's own leg, body and head wound severity, 0..1 (see Wounds). Contract 5 checkpoints do not load (the observation grew); the reward is unchanged.
- Actions (contract 5, heads `{5, 5, 3, 2, 8, 5}`): throttle, turn, strafe, jump, an action head (none, attack, eat or drink, call, ability 1, 2, 3, secondary attack; eat turns into drink when facing water) and the head aim (-1, -0.5, 0, 0.5, 1 of the species' neck arc; the stegosaur's tail arc behind it). Every RL agent has all its species' abilities. Contract 4 checkpoints do not load (the action heads changed).
- Reward per tick, with weights in `[env]`: alive, food and water gained, damage dealt (fraction of the victim's health), damage taken, kills, death, growth, quests completed.
  Dealt and kills pay by what the victim is worth (`cret_reward_worth` in `sim.c`): for a carnivore, the victim's mass over its own, at most 1 (a tyrannosaur killing a raptor earns a hundredth of a kill). A herbivore earns dealt only while defending (on a carnivore that struck it or its group in the last 10 s) and never a kill reward; a sauropelta's spikes pay nothing. A kill is credited only within 10 s of the last hit or while that wound still bleeds (an animal that bleeds out is its biter's kill; one that dies of thirst after the wound closed is nobody's). `docs/testing.md`, "Reward design", says why.
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
| **terra** | `terra/terra.[ch]` | libc, OpenMP (optional) | Offline world bake (see the pipeline below). Outputs one `.terra` file holding height, water level and kind, flow, soil, temperature, 8 material splats, 8 vegetation layers and the rock shoulders it cut. |
| **world** | `world.[ch]` | terra, Box3D | Loads or bakes the cache, builds the Box3D height-field collider, and loads the prop collision shapes from each prop's LOD1: a hull per variant (checked against LOD0 by `test_prop_colliders`), four overlapping slabs for logs and driftwood, meshes for roots, coast and cliff pieces. Tree trunks (conifers, broadleaves, palms, tree ferns, cycads with a stem of 1.2 m or more) are static capsules per 64 m tile (`cret_tree_trunks`), sized from the generator's own species dimensions (`veg_species_dims` in `render/veg_place.h`, which `render/veg_gen.h` builds the meshes from), so every collider has a drawn trunk; ferns, grass, shrubs and low cycads stay walk-through. Bots feel ahead for trunks and rocks (a capsule cast every fourth tick, `avoid_props` in `sim.c`) and veer round them. |
| **props_place** | `render/props_place.[ch]` | terra | Deterministic prop scatter shared by the renderer and the colliders, so what you see is what blocks you. |
| **flora** | `render/flora.h`, `render/veg_place.h` | terra | The species table (31 species over the eight bake layers, plus lilies, clubmoss and fungi), habitat, stands and gaps, and how a plant picks its species; shared GL-free by the renderer, the trunk colliders and the food query `cret_food_kinds_at` (`world.h`). [docs/flora.md](docs/flora.md) |
| **dynprops** | `dynprops.c` (included by `sim.c`) | sim, Box3D | Movable props: boulders, big stones, logs and driftwood are Box3D dynamic bodies, pushed by animals and crushing them (below). |
| **sim** | `sim.[ch]`, `eco.[ch]`, `bots.c` | world | Authoritative 30 Hz sim: species table (size, gaits, turning, stamina, jump), agents, actions (eat, drink, call, attack, rest), swimming, day cycle, observations. `eco.c` (included by sim.c): energy and fat, plants as a regrowing resource, perception, risk and the landscape of fear, the hunt arithmetic; `bots.c`: the scripted animals' drives and motor ([docs/ecosystem.md](docs/ecosystem.md)). Prop colliders stream in per 64 m tile around agents. |
| **net** | `net.[ch]`, `netclient.c`, `netjoin.c`, `netaoi.c`, `netcheck.c`, `nettelem.c` (included by `net.c`), `netsim.[ch]` | sim, ENet | Per-client delta-compressed snapshots with interest management, sequenced inputs, client-side prediction with reconciliation (movement and the player's own ability casts), interpolated remote agents, respawn/species requests answered once settled, abilities, statuses, quests, nests and character names, a link conditioner; hit events for combat feedback, movable props, critters, carried food, lag-compensated melee hits and lunges (the server judges a client's swing where it drew its targets), knockdowns, joining while the world loads (`netjoin.c`), a predicted slot-3 switch, wounds, versioned bit-packed bounds-checked codec (version 23), snapshot rate a server setting (15, 20 or 30 Hz); an area of interest that sends each client only what its animal could perceive (`netaoi.c`), server checks (`netcheck.c`) and a telemetry log for cheat classifiers (`nettelem.c`, `tools/cret_telemetry.py`). [docs/net.md](docs/net.md), [docs/anticheat.md](docs/anticheat.md) |
| **critters** | `critters.[ch]` (included by `sim.c`), `netcritters.c` (included by `net.c`), `render/critters.[ch]` (included by `render/view.c`) | sim | Small prey around people: a pool of plain structs on the terrain, no bodies or RL slots (below). |
| **scent** | `scent.[ch]` (included by `sim.c`), `hunt_hud.c` (included by `hud.c`), `render/scentfx.[ch]` (included by `render/view.c`), `shaders/scent.*` | sim | Trails, wind and smell; what an animal notices of small prey; blood on the trails of the wounded, which hunting bots follow (recorded with nobody about too, for the bleeding only); the hunting HUD and the scent view (V). Presentation plus queries: not in the RL contract (below, docs/progression.md). |
| **wounds** | `wounds.c` (included by `sim.c`) | sim | Lasting wounds by place from hard blows, their healing and worsening; the effects are read where movement, stamina and the senses are (constants `CRET_WOUND_*` in `sim.h`; see Wounds). In the RL contract since contract 6 (own wounds). |
| **abilities** | `abilities.[ch]` | sim | Ability table, loadouts and unlocks, status effects (stun, fear, slow, latch, defensive, marked). |
| **progress** | `progress.[ch]` | sim, world | Named places from the world data, quests, nests and eggs. Carrying food is in `sim.c` (below). |
| **character** | `character.[ch]` | sim, abilities | Saved characters in `~/.config/cretaceous/characters/<name>.ini` (`%APPDATA%\Cretaceous\characters` on Windows). Player and server only. |
| **gfx** | `render/gfx.[ch]` | raylib (window and GL loader only), GL 3.3 | Shader loading with recursive `#include`, render targets and texture helpers. |
| **view** | `render/view.[ch]` | gfx, terra | Terrain, water, atmosphere, volumetric clouds and post-processing (details below). |
| **veg** | `render/veg.[ch]` | gfx, terra | Procedural plants, impostors and instanced drawing (details below). |
| **csm** | `render/csm.[ch]`, `shaders/csm.glsl` | gfx | Cascaded sun shadow maps: plants, props and agents cast; terrain, plants, props and water receive. |
| **dino** | `render/dino.[ch]`, `render/dino_ik.[ch]` (included by `dino.c`), `shaders/dino.vs/fs` | gfx, sim | Skinned dinosaurs from `assets/dinos/*.dino` (details below); the foot IK has no GL. |
| **nest** | `render/nest.[ch]` (included by `render/view.c`), `shaders/nest.vs/fs` | gfx, sim | Nests, their eggs, nests being built, and eggs and caught critters lying loose or carried (below). |
| **player** | `cretaceous.c` | all | Solo, host, join, server and capture modes. |
| **capture** | `capture.[ch]` | view, sim | Scripted camera paths rendered frame by frame and piped to ffmpeg (`capture --path`). |
| **fx** | `render/fx.[ch]`, `shaders/fx.vs/fs` | view, dino, hud, sim | Combat feedback from the sim's hit log (details below). Presentation only. |
| **audio** | `audio.[ch]` | raylib audio, sim | Ambience beds mixed from the world around the listener, positional footsteps, calls, attacks, hits, eating, drinking and deaths, and menu sounds (details below). Player only; the trainer never links it. |
| **menu** | `menu.[ch]` | hud | Title screen over a flyover of the island, settings (quality, render scale, display, interface size, mouse, camera shake, hit-stop, volumes, key list) saved to `~/.config/cretaceous/settings.ini` (`%APPDATA%\Cretaceous\settings.ini` on Windows). |
| **hud** | `hud.[ch]` | raylib, sim | Health, stamina, hunger and thirst bars, growth, injuries, compass, minimap and island map, group panel, group-coloured nameplates, rally marker, ability bar with cooldowns and status chips, quest tracker and markers, death screen, pause menu and credits (read from `assets/dinos/CREDITS.md`). Laid out for 1080 lines and scaled to the window height (1.0 at 1080p), times the Interface size setting (50% to 200%; above 100% capped so the HUD keeps 600 layout lines and the menu screens 880); fonts re-rasterise at larger sizes above 1080p. |
| **binding** | `cretaceous.h` | sim | PufferLib env: every agent is a learner. |
| **policy_cpu** | `policy_cpu.[ch]` (included by `cretaceous.c`) | sim | Trained checkpoints on the CPU for the player, the server and `cret_eval` (`--policy`, `cpu:`): both encoders, the MinGRU, the decoder and the action sampling, recurrent state per slot (docs/encoder.md). |

The terra bake pipeline:
- tectonic plates and uplift;
- multigrid stream-power erosion with a channel threshold, sediment deposition, implicit diffusion and mass-conserving talus toward 20° (soft rock) to 37° (hard beds), then a hard slope cap a little above it (about 34° soft, 39° typical, 51° on the hardest beds). The relaxation alone could not keep up with uplift and incision on the 8 m cells late in the evolution and left 55–60° spires ("mouse-pulled" peaks); with the cap, summits have broad bases, ridgelines and eroded shoulders (version 20: land steeper than 45° fell from 23% to 4%, the mean half-height radius of the main peaks grew from 280 m to 620 m, the highest summit is about 630 m instead of a 1140 m spire);
- orogens 0.065–0.1 of the map wide, segmented along their length; hotspot volcanoes as broad warped shields with long concave flanks and a summit crater;
- droplet erosion and lakes from a priority flood;
- coastal flats: on about half the coastline, land near the sea is lowered onto a gentle beach profile (35–110 m wide) and the seabed offshore is raised into a shallow shelf;
- rock shoulders: curved notches cut into valley sides only as high as the rock that stands in them (version 23; see Rock shoulders);
- rivers carved along flow;
- surface materials and vegetation from moisture, temperature, slope and curvature;
- at load (not saved, so the world file and its version stay): a half-resolution habitat grid (moisture index, nearness to rivers and the sea, landform) from the fields every build ships (`terra_habitat`), which picks the species of every plant (docs/flora.md).

The view module covers:
- quadtree-LOD terrain with a B-spline-prefiltered height texture, so the smooth surface passes through the baked samples; the sim stands animals on the same surface (`cret_height` samples the world's shared B-spline coefficients), so drawn and simulated ground agree;
- height-blended triplanar materials and horizon-angle terrain shadows;
- precomputed sky (transmittance and sky-view LUTs);
- volumetric clouds (below): a weather map with cloud types, Perlin-Worley shape and Worley erosion, a sparse full-resolution march accumulated across frames, thin altocumulus and cirrus layers;
- moonlit nights (see Night below), with a moon disk, twinkling stars and a faint Milky Way;
- weather driven by the cloud cover: overcast deck, grey sky light, rain streaks, haze and wet surfaces;
- water;
- bloom and tonemapping;
- reverse-Z depth in a float buffer (no shader writes depth), so plants get a depth prepass and terrain and plants shade each pixel once.

The veg module covers:
- a procedural foliage atlas and procedural species meshes;
- deterministic per-tile scatter from the terra vegetation layers;
- 8-view impostors baked in-engine, with albedo plus normal and AO, the two nearest views blended;
- instanced drawing with wind and shared lighting (`veg_light.glsl`);
- tall grass, sedge and reeds that animals push through (below).

**Species** (`render/flora.h`, [docs/flora.md](docs/flora.md)): every bake layer's 8 variant slots hold its species (one or two seeds each): 5 conifers, 6 broadleaves, 3 cycads and bennettitaleans, 4 ground ferns, 2 tree ferns, 2 horsetails, 2 palms and 4 shrubs and herbs, picked per plant by habitat and a per-species stand noise; plus standing dead trees in old stands (`VEG_SNAG`), water lilies on quiet water (`VEG_LILY`), clubmoss and moss in the sward, fungi on stumps and logs (`VEG_FUNGUS`) and fallen fruit, seeds and cones under the plants that bear them (`VEG_DROP`, near the camera). Fruit, cones and flowers are meshes on the plants (`add_fruit`).

**Trees and tall plants** (`render/veg_gen.h`, built on the CPU at startup in parallel, well under a second): every tall kind has 8 variants (its species' slots), plus per-instance scale, hue, yaw and a lean of up to about 2.5 degrees.
Each plant grows from a skeleton of tapered tubes (parallel-transported frames, continuous bark uv, flared joints). Foliage hangs on it as many small alpha-tested cards and leaflets, oriented in 3D around their twig or rachis, so a crown holds up from any side and from below.
- conifer (heights at scale 1; instances scale 0.7-1.25): Norfolk-like araucaria, 25-32 m (whorled tiers of slim branches clothed in opposite pinnate sprays that droop a little, swept forward; the reduced mesh lays whole fronds along each branch), podocarp, 16-21 m (limbs rising off a leader, each ending in rounded pads of strap-leaf shoots: a dense, dark, lumpy column), umbrella araucaria (below), metasequoia, 32-39 m (buttressed, a narrow cone of branches every 0.3-0.5 m with opposite side shoots in shallow layers of light green feathery sprays) and cheirolep cypress, 15-22 m (a narrow flame-shaped column of tall pads of scale-leaf shoots on steep limbs, the trunk often leaning). Ages (`veg_conifer_age`): redwood variant 3 is young (17-23 m, clothed to the ground) and 7 old (40-46 m, a high, broad, rounded crown with dead lower limbs), araucaria 4 old (34-40 m, bare trunk with stubs to nearly half its height under a flat-topped crown) and podocarp 5 old (20-23 m, broad crown on a tall bole);
- broadleaf: magnolia, laurel, multi-stemmed katsura and a drooping understory tree, forked limbs inside a lumpy crown envelope with twigs carrying photo leaf clusters;
- cycads (rachis tube and keeled leaflet pairs, a skirt of dead fronds) and Williamsonia (forked, a cone at each crown);
- tree ferns (bipinnate arching fronds, croziers, a dead skirt) and palms (costapalmate fans of folded segments, or feather palms);
- shrubs (a small branching crown; the gunnera a clump of big dished leaves on stout stalks, the flowering herb leafy stems over a rosette of basal leaves) and horsetail colonies (jointed stalks with needle whorls and spore cones).
- Trunks bow (`mesh_bow`): the finished mesh is sheared by a smooth sweep and a gentler S bend over the bottom two thirds of its height, from its own seed, so no crown draw changes and both meshes bow alike (broadleaves most, the redwood and araucarias least). Round 2 of the look hill-climb (`docs/hillclimb/near/LOG.md`) also gave the chloranth, magnolia, Williamsonia and squat cycad fuller crowns and real leaf sizes.
A coarse leaf-area grid over each mesh bakes every vertex's sky visibility (AO and crown depth) and bends foliage normals outward.

**Trunks and bark** (trunks hill-climb, `docs/hillclimb/trunks/LOG.md`). No trunk is a plain cylinder:
- *Bark by species*, each a CC0 photo scan laid at about its real size (1 to 1.8 m a tile, `bark_tile_m` in `render/veg_layers.h`), with its own furrow shade (AO and the low end of its height map) baked in by `tools/fetch_foliage.sh`: fibrous red-brown strips on the dawn redwood, plates on the araucarias, fibrous strips on the podocarp, grey furrows on the ginkgo, smooth grey on the other broadleaves, and the plane's bark mottled cream, grey-green and olive in `veg.fs`. A macro layer (the same scan at a quarter of its frequency) keeps broad strips and plates on trunks 20 to 60 m away and hides the tile's repeat up close. Each species has a real mean bark albedo (0.1 to 0.19).
- *Relief* (`Relief`, `bark_relief_init`, `relief_at` in `render/veg_gen.h`): a root flare with 4 to 9 buttress roots, flutes fading up the trunk (deep on the redwood and podocarp), and bumps; trunk rings every 0.25 m in the bottom 2 m, and normals taken from the surface itself, so the flare, flutes and taper shade. Tree ferns and palms take it too (a root mass or root boss, a shaggy or uneven column). The reduced mesh keeps the flare and the broad shapes.
- *Limbs* are 20% heavier than grown, swell into a collar where they leave their parent, and bend through Catmull-Rom midpoints (full mesh) instead of polyline elbows. A few broken, mostly weathered stubs stand on the lower trunk.
- *Shaded patterns*: the cycad's diamond leaf bases are domed scales, the tree fern's trunk a mat of root fibres and the palm's rings ridges, as bumps in `veg.fs` that catch the sun. Palms also take ring bands, weathering streaks and a darker foot, tree ferns broad patches of rust root mat, so they are not smooth columns at 10-30 m; the plane's mottling is warm (fawn, khaki-olive, cream-yellow).
- *Under a low crown*: bark of cycads, tree ferns and palms bakes its sky visibility from its sides (`bake_sky`'s `side`), and takes 1.6 to 2 times the leaves' ground bounce (`veg_bounce`, set per bark tone in `veg.fs`): those trunks drew near black under their own crowns.
- *In a forest* bark takes the canopy closure over its foot on its sky light, as the floor does (it stood out of shaded forest as pale blue-grey poles).
- *Leaves seen from below* let the sky light on their upper face through (`veg_light.glsl`, meshes only), and dead fronds are a real dead-frond brown, not a darkened green (`veg_dry`).
- `CRET_BARK_RELIEF=0` and `CRET_BARK_STUBS=0` (development only) draw the trunks without the relief or the stubs, for comparison.
Leaf clusters, leaflets, segments and cords are new atlas layers (`L_CL_*`, `L_LEAFLET`, `L_PSEG`, `L_ROPE`, `L_FPINNA`, `L_SCALES`, `L_HSTEM` in `render/veg.c`), cut from the photo sprites or painted when those are missing.
The full mesh (with twigs, which cast no sun shadows) is drawn within `tree_lod_dist` (32 m on high, a little less for the small tall kinds), then the reduced mesh (no twigs, merged leaf cards, fewer leaflets) with a dithered crossfade, then the impostors past `CRET_VEG_NEAR`. Both switch distances scale with the tree's size on screen: the square root of its height over its kind's mean height, within 0.85 to 1.15 (`tree_switch_scale`), so a tall tree keeps each level further out than a small one of its kind. The impostor fades in as the exact complement of the mesh's fade-out (the same screen noise, the other pixels), so the crown keeps its coverage through the swap; the two used independent noise and a crown thinned to about 75% coverage halfway. The impostors are baked from the full mesh at startup (albedo 256², normal and AO 128²). Each blends the two baked views either side of the view azimuth (across the middle 60% of the band between them; the low preset picks one per pixel), lights its bark texels as bark (they were lit as leaves and glowed as bright sticks under dark crowns) with the crown's baked occlusion, and raises its coverage by half per mip level down, so a tiered conifer keeps its crown at distance instead of thinning to a stick.

The three levels are kept to one tone: each kind and variant has a leaf tone for its reduced mesh and for its impostor against its full mesh (`LOD_TONE`, `IMP_TONE` in `render/veg.c`), fitted with the gallery's LOD-match pages and `tools/lodmatch.py` to within about 5% at 30 m (reduced) and 150 m (impostor). Impostors read their textures one mip level sharper than their footprint (`IMP_BIAS` in `veg_imp.fs`), so a far crown keeps its lit sprays and shaded hollows instead of a soft smear. Past 30-80 m each tree's leaf tone is spread by -32% to +16% and its hue shift widened (`veg.vs`, `veg_imp.vs`), the same on every level of the tree, so a far stand is a mottle of single crowns. Trees in a stand also take the stand's canopy (the terrain's canopy density) on the sky light of their trunks and lower crowns past 15-40 m, and past the shadow cascades on part of their sun as well (`veg_light.glsl`): trunks under a forest no longer stand out as pale poles, and far stands have shade between their crowns.

Each species has its own green on top of its leaf layer (`LEAF_TINT` in `render/veg.c`, by kind and variant), set per draw on the full and reduced meshes and per variant on the impostors, so a tree keeps its green at every level: blue-black araucaria and cheirolep cypress, warm podocarp, fresh dawn redwood, dark laurel and magnolia, cool katsura, yellow-olive ginkgo and plane, olive cycads, deeper tree ferns. The haze over plants (`veg_shade` in `veg_light.glsl`) keeps only a fifth of the sky's colour out to 600 m (the rest grey, its brightness kept), rising to 70% by 3 km: with the full horizon blue, dark crowns 100-600 m off turned teal, where photos of forest across a valley go grey-green; past a few kilometres the stands take the blue of their hills again. Ground cover is less saturated than tree crowns and takes a lighter green filter on its sky light, so sunlit fern prairie is not neon lime and shaded forest ferns are not emerald.

The full and reduced meshes of a variant are the same plant with the same skeleton: every branch path is grown once at full resolution on both meshes and the reduced one only draws it through every other ring (`path_coarse`), its leaf cards are every third of the full mesh's cards at the same points and rolls (merged larger, `leaf_cards`), and anything whose number of random draws depends on the detail level runs on a forked stream, so the crossfade only trades detail and no branch moves. The reduced meshes used to be grown with fewer, longer steps, which put every limb somewhere else and showed as branches jumping at the switch.
`cret_veg_trunk_radius(kind, variant, scale)` gives the base trunk radius from the same seeds without building a mesh (trunk colliders, `cret_veg_cast_trunks`).

**Tall grass** (`VEG_GRASS` in `render/veg.c`): clumps of arching ribbons textured with blade-photo strips (atlas layers `L_GRASS`, `L_SEDGE`, `L_REED`, pinched at the base into a tuft).
- Variants: meadow grass (about 0.5–0.9 m), tall grass (0.8–1.3 m), a sedge fountain, and reeds with a sedge skirt (1.1–1.6 m) that may stand in 15 cm of water.
- Placement is regenerated around the camera on a 0.8 m lattice. Density follows open living ground (fern prairie, soil, mud, some moss and litter; never rock, scree or sand), thins under a closed canopy, and is highest at lake and river edges, where sedge and reeds take over. A ~6 m tussock noise inside ~40 m meadow patches makes it clumpy.
- Drawn out to half the ground-cover range (61 m on ultra, 45 m on high, 36 m medium, 27 m low), full mesh within 15 m (scaled by preset) and a 16-triangle mesh beyond. It shrinks a little and dithers out over the last 30%. Casts into the first shadow cascade within 16 m (on high and ultra also into the second, outside the first). Ultra scatters 40% more clumps (and sward tufts).

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
- foot IK near the camera (LOD 0 and 1), in `render/dino_ik.c` (no GL; `dino.c` includes it, `tests/test_dino_ik.c` tests it on synthetic ground): legs found by joint name (hip, knee, ankle); each foot keeps its animated height over the real ground under it (terrain, or a rock or log's top through `cret_ground_at` when that top is within half the hip height of the animation's ground, so a foot in its swing does not step onto a taller prop), the pelvis drops so the lowest foot reaches (no further than the highest foot can fold), knees keep the axis of their animated bend plane (no twist about the leg on a cross slope), and planted feet tilt about their contact toward the ground normal; fades out in the air, swimming and in death. Measured on the test dinosaurs: reachable feet end 0.00 mm off, from flat ground to 45 degree slopes, a step and a rock top (docs/testing.md);
- lighting shared with the props (sun, cascades, sky ambient under the canopy, wet skin below the waterline).

`tools/dino_convert.py` (headless Blender) turns an animated glTF into a `.dino` file:
- skeleton pruned to weighted joints, 4 weights per vertex;
- model scaled to a given length, facing +x, feet on the ground;
- clips resampled at 30 Hz, with root motion removed and the ground speed measured from the planted foot (the root's drift only when no foot joint is found: adding both stored root-motion clips at twice their speed);
- LOD0 within a triangle budget, decimated LOD1 and LOD2;
- 2K body textures (albedo, normal, AO, roughness, metal).

`tools/animcheck.c` measures the converted clips without a GPU (foot slide, the feet's speed against the stored speed, feet under the ground, loop seams, bone length drift, joint pops, and the planted-foot slip of the pose the game blends at each sim gait): `make -C ocean/cretaceous animcheck && ./build/cretaceous/animcheck` from the repository root checks every model in `assets/dinos` (docs/testing.md). `tools/blender/make_fixture.py` builds the tiny test dinosaurs in `tests/fixtures` it and the IK test run on.

Third-person camera (`camera.h`, stepped by `update_camera` in `cretaceous.c` and by the headless probe `tools/camprobe.c`): the pivot sits just behind the shoulders at 1.05x the animal's height, and the boom is 1.5 body lengths at zoom 1, so every species fills the frame alike.
On flat ground the look pitch stays between 0.55 rad down and 0.45 rad up. Where the ground ahead falls away the view may look further down (0.9 of the slope, to 1.25 rad), where it rises further up (to 1.0 rad), so looking down or up a hill works.
The boom is a 0.3 m sphere swept from the pivot. It stops at prop colliders (Box3D) and tree trunks (`cret_veg_cast_trunks`), pulls in at once and eases back out (2.5/s, at most 1.5 booms a second); easing out waits rather than move the lens behind a rock or ridge.
Terrain and water do not stop it: the lens rides up over them, 0.45 m clear of the highest ground within 0.4 m (so it stays clear square to a slope too) and of the water as drawn (the level blended between cells, sunk under the banks where the wet mask fades, plus the sea swell). It rises at once and sinks back over a sixth of a second, so it does not dip into every hollow.
Where lifting would lose the animal the boom shortens instead: the terrain or a prop between lens and pivot pulls it in at once; the pivot falling more than 0.4 rad below the centre of the view eases it in. Pitching up into the ground behind stops when even the shortest such boom would put the pivot 0.47 rad below the centre, so the animal never drops off the bottom of the picture.
A boom shorter than the body goes over the animal's back (0.3 m clear of a capsule along it), never inside the model. The pivot lags the animal by at most a tenth of the boom vertically and never sinks into a crest it has run over.
Nothing turns see-through; only leaves within 0.6 m of the lens are cut.

Mouse look: the cursor is captured in play. Raw motion is on where GLFW supports it, except under WSL, where WSLg's absolute pointer makes raw deltas into positions.
The first frames after a capture, a focus change or a resize are dropped. A jump of more than a quarter of the window in one frame is thrown away, and three in 2 s turn raw motion off.
Sensitivity 1 is 0.0025 rad per pixel.
Presses (attacks, call, jump, abilities) are latched until the next 30 Hz tick, so a click in a frame without a tick still counts.

Combat rules (`sim.c`: `move_agent`, `lunge_step`, `resolve_attack`, `resolve_alt`, `damage`; numbers in `CRET_SPECIES_TABLE`). Bots, people and policies all go through them; nothing depends on the input device.
- **Aim.** Attacks aim where the camera looks, within the species' neck arc (70 degrees either side for the raptor down to 35 for the stegosaur's tail, behind it). The strike point is at the jaws (or the tail tip): the neck base 0.18 body lengths ahead of the centre, turned by the aim, the snout 0.24 lengths beyond it.
- **Squaring up.** An animal standing still (no move or turn key, no step) that presses a primary attack, has one queued or is winding one up turns its body toward the blow at no more than its own turn rate: 3.6 rad/s for a raptor, 2.6 for a pachycephalosaur, 1.2 for a tyrannosaur, 1.1 for a stegosaur (faster for the young). A bite, headbutt or bash turns the body only while the aim is at the end of the neck arc (`CRET_SQUARE_AIM`, 0.98: the target is further round than the head reaches), so an aim inside the arc still only turns the head (the policy's aim head keeps its meaning). A tail blow brings the tail square onto its aim, because the camera's tail aim fades in behind the hips and seldom reaches the end of the arc. Before, the body turned only while a move key was held, and a standing pachycephalosaur circled by a raptor butted empty air: 193 of 303 blows "passed" in `combatprobe`'s `pachy<fraptor`, now 5 of 210 (docs/testing.md).
- **When it lands.** The blow lands `hit_time` into the swing: 0.3 s (gallimimus), 0.35 s (raptor), 0.4 s (sauropelta), 0.55 s (stegosaur, pachycephalosaur), 0.85 s (tyrannosaur). The quick animals snap and the tyrannosaur's bite stays heavy (the raptor's was 0.5 s, the pachycephalosaur's 0.7 s: press to hit 0.58 and 0.77 s, now 0.46 and 0.64 s). The swing starts on the tick of the press; the cooldown (1.2 to 2.6 s) and the swing's length run from there and did not change, so damage per second is the same. The attack clips follow `hit_time` (`render/dino.c`: the wind-up plays at clip hit / `hit_time`, at most 2.5x, starting partway into the clip when that is not enough; after the blow the recovery plays at the rate that ends the clip with the swing).
- **Reach.** A blow lands if the victim's body (a segment along its heading, 0.8 of its length, plus its radius) is within `reach` of the strike point: 1.0 m (pachycephalosaur, gallimimus) to 2.6 m (stegosaur's tail). A bite lands on one animal, the body nearest the jaws; tail swipes and slashes sweep everything in their arc.
- **Lunge.** Through the wind-up, a bite, headbutt, bash or kick in front carries the attacker toward the body it would land on (one within reach plus the lunge of the jaws, within 60 degrees of where the blow points): each tick it takes its share of the gap the blow would have at the hit, counting how fast the target draws away, aiming to have the body half way into reach. Over the whole wind-up it moves at most a quarter of the attacker's body length (`CRET_LUNGE`), or as far as the animal can throw itself from a stand at its own acceleration in the wind-up (half accel x `hit_time` squared), whichever is shorter: 0.55 m for a raptor, 0.9 m for a pachycephalosaur or a tyrannosaur, 0.36 m for a gallimimus, 0.16 m for a sauropelta. A target moving away is followed further, as far as it moves in the wind-up at up to the attacker's own trot, still within the quarter body length: up to 0.75 m for a raptor, 1.1 m for a pachycephalosaur, 2.7 m for a tyrannosaur, 1.5 m for a gallimimus, 0.9 m for a sauropelta. A standing tyrannosaur biting a stegosaur that walks off landed 86% (0.9 m fell short of the 2.5 m/s walk through the 0.85 s wind-up), now 99%. It goes through the mover, so bodies still block it, and only on the ground. Tail blows and charges do not lunge. A networked player's lunge, like the hit test, reaches for the target where the player saw it (docs/net.md, Lag compensation), and the player's own client predicts the same lunge.
- **Moving while attacking.** Attacks keep 70 to 92% of the gait until the blow lands and never root the animal (slashes and kicks keep all of it).
- **Queue.** A press while the attack cannot start (swinging or cooling down) waits up to 0.3 s (`CRET_ATTACK_BUFFER`, 9 ticks) and starts the moment it can; an earlier one is dropped. A queued bite keeps waiting while the animal is off its feet (thrown by a blow) or while its target draws away faster than the lunge could follow by the hit, and lapses if, when it comes due, nothing is within reach and lunge in front any more. A fresh press always swings, and so do queued tail blows, sweeps and slashes.
- **Blows.** Damage by growth squared, less armour (heavy blows punch through it), bleeding, a flinch (0.4 s, 0.8 s for headbutts and tail swipes; movement at 30% for its first part), knockback by the mass ratio that throws the victim off its feet above 1.5 m/s (its own swing still runs and usually whiffs), broken bones from crushing blows or hard throws, and a lasting wound from a blow of 8% of the victim's health or more (see Wounds). Spikes (sauropelta) cut what bites them from the front.

Carnivores eat from a carcass at the jaws or lying beside the front half of the body; the HUD says when E will eat, how far the nearest carcass is, when a bite would catch small prey and how far the nearest small prey is.
The secondary attack (right mouse) is a tail swipe with mass-scaled knockback for the tyrannosaur, pachycephalosaur and sauropelta, a wide tail sweep for the stegosaur, and a quick pass-through slash or kick for the raptor and gallimimus.
The head turns toward the aim. On the move, attack clips play over the neck and head only, and tail swipes are procedural (`render/dino.c`).

These pieces could stand alone as open-source repos: **terra** (terrain baker), **sky + clouds** (the view atmosphere passes) and **veg** (procedural plants and impostors).
Once a second game uses them, `gfx` would become the shared base library.

## Bedrock

The mountains show their rock, and the rock is the land itself, not boulders put on it. No rock scan is big enough for a face 30 m tall and 200 m long, so the faces and shelves are the heightfield, and the ground shader draws them as one mass of jointed, bedded rock (`docs/terrain/bedrock/README.md` has the pictures, before and after from the same cameras).

What the bake does (`terra/terra.c`, after the droplets and the talus, before the hydrology):
- **More uplift under the high ground** (`peaks=0.5` in `CRET_TERRA`; 0 turns it off): ground above 220 m is raised by half its height over 220 m, then held to the talus slope of its rock (about 39 degrees on ordinary rock, up to 51 on the hardest beds), as the evolution does. The highest point goes from 632 to 719 m (gcc build, as the owner builds). Nothing under 220 m moves.
- **Hard beds weather into benches and cliff bands.** The rock is a stack of beds, a soft bed and a hard one every 55 m, tilted and warped (`strata`). Where a hard bed crosses a mountain side, the soft bed above it wears back and leaves the hard bed's top as a bench, and the hard bed stands as a cliff band 8-33 m tall (by how hard it is) over the soft slope below. The profile only ever takes ground away and leaves every hard bed's top where it was. On mountain sides above about 320-380 m it works at half strength in patches (crags), and round each bedrock shelf at full strength.
- **Bedrock shelves** (`bedrock=1`; more or fewer, 0 none): a few landmarks per mountain range, 14 on the island (10 on mountain sides, 4 smaller ones on hills under 200 m). Each is a flank worn back into the mountain: a scoop 80-300 m across whose floor is a bench rising gently into the mountain (it drains out of its mouth), whose inner wall is a bedrock face 8-40 m tall rising at about 73 degrees, its rim the top of a strong hard bed, buttresses and gullies along it on the joints, and the face dying out into the hillside at both ends, so the spurs either side remain. Sites are picked where the mountain rises at least 50 m above, the valley below is at least 30 m lower and its far side rises again (so from the floor it is a valley), and the face's rim falls on a hard bed's top.
- **Regolith** (the loose ground over the rock, `TerraMap.soil`): about 1.5 m on gentle ground, none past about 50 degrees, thinner on convex ground and high up (from 330 m) and over hard beds, gathering in hollows and on toe slopes, plus what water laid down.
- **Wind erosion** (new: there was no wind layer before). The prevailing wind (`wind_angle`) strips loose ground where it meets exposed ground: open upwind (nothing higher within 400 m upwind), more on windward faces and up high, less under forest. What it takes rides the wind and settles in the lee, where the ground is sheltered or falls away, on gentle ground; over the sea it is lost. On the island it moves 1.35 million m3 and strips more than 0.3 m from 6.6% of the land (`bake --verbose` prints it). Water lowers the rock first (stream power, droplets); the wind only moves loose ground.
- **Where the loose ground is gone the ground is rock**: bare rock where the regolith is under about half a metre on anything but flat ground, coming in from 280 m and fully from 420 m (lower down it would take the mid slopes' plants: a first version with bare rock from 200 m starved a pachycephalosaur herd grazing at 230-330 m). Bare rock as the main material, by height (`terrashade --relief`): under 100 m 0.2% (was 0.2%), 100-200 m 0.3% (0.3%), 200-300 m 1.0% (0.5%), 300-400 m 18% (1.1%), 400-500 m 54% (1.5%), 500-600 m 74% (2.0%), 600-700 m 82% (5.0%); rock or scree 25%, 70%, 90% and 96% from 300 m up.
- **A tree line** at about 380 m (lower on exposed windward ground): conifers go highest, the broadleaf groves stop below them, scrub carries on as a low thicket, then thin ferns, moss, scree and rock. Tree density (conifers and broadleaf, 0..1): 300-400 m 0.36 (was 0.71), 400-500 m 0.01 (0.70), above 500 m 0 (0.53-0.65); below 300 m unchanged (0.60-0.76). Near faces the trees stand back and moss and ferns take the benches and ledges. All of it through the bake's splat and plant layers.
  - The line follows the land: it rises up to 45 m in hollows and 40 m up the gullies and falls as much on spurs, so the forest climbs the gullies in fingers with bare spurs between. Above it, for another 60-140 m, the last conifers stand in tree islands: tight clumps of a few stems, drawn out along the prevailing wind, each with a skirt of scrub that reaches further on its downwind side, with bare stony ground between, so the forest thins out instead of stopping.
  - Mountain gullies (above 160-260 m) are seams of fern and moss, not the bare erosion soil they are lower down.
- **The grass line** (the mountains look hill-climb, `docs/hillclimb/mountains`): from about 40 m over the tree line to 230 m over it (sooner on exposed crests, later in hollows) the green fern turf thins to scattered brown-green tufts, and no green is left within about 150 m of a summit; the summits are rock, scree and stony soil. The ground shader tints what living cover there is above 400-560 m a dry grey-brown and greys bare soil above 360-500 m, so neither lowland green nor orange lowland earth reaches a summit.
- **Scree fans** under the cliff bands: from each sample the bake walks up the fall line, and rock steeper than 45 degrees within 12-35 m (a noise along the foot makes the fans come and go) and at least 8 m higher lays scree, fading with distance from the foot, mostly above 150-380 m; trees thin by half on it and ferns by 70%. The scree is a talus of angular grey blocks (Poly Haven gray_rocks), in two sizes mixed by a noise, with dark voids between the blocks within about 100 m.
- The rock each sample shows (`TerraMap.hardness`) is now the bed at its final height, so the shader's beds match the cut faces.

The ground shader (`shaders/terrain.fs`, Bedrock): rock ground varies in value and colour at 15-80 m (cool grey to ochre), read in the face's own plane on steep ground so nothing is smeared down the fall line; the hard beds read as pale massive bands and the soft beds darker and redder, with finer layers in each; on faces over about 55 degrees the rock is drawn as big jointed blocks (bed partings every 1.8-5.5 m, joints every 4-12 m offset from bed to bed, each block its own tone and a gently rounded face, dark cracks holding moss where it is damp, shadow under each overhang), fading out by 600 m; faint dark streaks run down steep rock; up-facing rock catches soil and moss below about 420-540 m and only grey grit above. On a face the patterns are read in the vertical planes facing x and z, blended by the normal (a coordinate along the normal's own tangent turned with it and drew sawtooth stripes and swirls). Within about 120 m rock is fractured: slabs of 1.5-4 m and, within 30 m, plates of 0.4-1.5 m inside them, each with its own tone and tilt, with wandering dark cracks, read triplanar (high and medium presets). Bare stony soil above the tree line is drawn dark grey-brown (its pale photo read as cream blotches down the high slopes). The alpine hill-climb (`docs/hillclimb/alpine`) has the before and after pictures. Dry-country straw tint no longer lands on rock. Cost (Mesa llvmpipe, so only a guide): the terrain pass on a view that is mostly rock face goes from 48 to 136 ms a frame against the forest that stood there before (most of it the rock's triplanar sampling); on a mountain view from the lowlands 32 to 33 ms. The block skin is off on the low preset.

Props: high up, rock ground is the mountain's bedrock, so the island's scatter puts no cliff pieces above 180 m, few outcrops, tors or boulders on rock above about 100-220 m, and fewer boulders on the high scree; real rock scans lie at the feet of faces as fallen blocks on the scree.

Walking: the faces cannot be climbed (over 53 degrees), but each one dies out into the hillside at its ends within 40-150 m of its middle, and the shelves' floors open downhill. Walkable land with no walkable way to water in the bots' water field (`terrashade --relief`, 64 m cells): 3.65% (3.22% before), most of the difference cells on steep mountain sides changing sides. A bot seeking water whose straight way to it crosses ground too steep to walk now follows the water field's leg round it (`bots.c`, seek water): walking the straight line, animals that spawned in a shelf's scoop milled about under its face until they died of thirst. The ecosystem traces are in `docs/testing.md` (Bedrock).

The rock shoulders (below) are off by default now: the bedrock faces replace them. At the shoulders' scale (3-9 m walls built from scaled-up rock scans) the rock read as small boulders in a pale notch; the owner's review asked for the rock face itself, at the mountain's scale. `shoulders=1` brings them back.

## Rock shoulders

Off by default since the bedrock shelves (above); `CRET_TERRA="shoulders=1"` bakes them. A hillside with a bite taken out of it and a rock in the bite: a short, curved notch cut into the side of a valley, its back wall a face of rock 3 to 8 m high with the hill rolling over its top, a dark floor of leaf litter, moss and fern in front and a strip of scree at the rock's foot. From afar it reads as a rock outcrop on a green hillside. They come in staggered pairs across some valleys (one bite on each side, 25-70 m apart along it), so walking down the valley you pass between them; nothing is closed in. They are part of the land, not landmarks placed by hand: the bake finds them in its own fields and cuts them into the heightfield, so the sim's ground collider, the water, the ground materials and the plants all follow the cut. `docs/terrain/capture_shoulder_*.jpg` show the four tallest in the real renderer (the previous version above, this one below), `docs/terrain/capture_shoulder_far.jpg` the same four from 200 m, and `docs/terrain/shoulder_*.jpg` six of them before (left) and after (right) from above and at an angle, drawn without a GPU.

The rock comes first. The bake plans each wall only as high as the rock that will stand in it reaches, and the rock layout then builds the wall from closed rocks sized to the wall where they stand, so no bare cut face shows behind or beside them.

Where (`terra.c`, `sh_judge`). One candidate every 16 m, judged on:
- the lower half of a valley side: 6-90 m above the lowest ground within about 110 m, at least 30 m above the sea, and out of the beaches' reach (not within 450 m of the sea unless 70 m up: the beaches the world lowers at load would stop at a shoulder near the coast and leave a bank over the beach);
- a slope of 0.3-0.75 (17-37 degrees, ground an animal walks round), falling on below the mouth to gentle ground (a valley floor or a saddle) at least 5 m lower within 200 m, so the bite opens onto open country;
- a straight valley side: not a hollow (no streams run into a notch) and not a spur's nose (along the contour the ground stays within 3 m of the mouth 15 m either way; on a nose the notch's ends fell away below its floor and left a perched shelf with no wall at its sides);
- the hill rising well above the planned wall, so it can roll over the rock;
- hard beds first (the bake's rock hardness and strata): the score is mostly hardness, then slope and a random share.

The best sites win, 380 m apart (`shoulders=K` in `CRET_TERRA` scales that to 380 / sqrt(K) m; `shoulders=0` turns them off). Each then looks across its valley for a partner: down to the floor, on until the far side has risen 6 m and faces back, then 25-70 m up or down the valley, judged the same way and at least 60 m from any other site. Everything is deterministic per seed and the same on any number of threads.

The cut (`shoulders_cut`, before the hydrology). In the notch's own frame the wall's foot is a curve `depth (1 - t^2)` across the mouth, skewed to one side and wobbled (no two alike), 14-36 m wide and 5-22 m deep. The ground becomes the smooth minimum of itself and the cut surface: a floor rising 5% toward the wall (it drains out of the mouth), a low heap of scree at the wall's foot (0.25 m + 4% of the wall high, 2.5 m + 15% of the wall out), the wall rising at 3:1 (72 degrees) until it meets the hillside, and past the mouth a rise again, so nothing beyond the bite is ever cut. The smooth minimum (1 m + 10% of the wall) rounds the lip where the hill rolls over. The planned wall is 3.5-7 m (taller where the hill is); the hillside still rises above the foot, so the bake asks for a rise of `wall x (1 - slope / 3)` to the foot and the wall comes out up to about 9 m.

Ground and plants (`shoulders_surface`, after the surface pass): rock and moss on the wall (behind the rock), moss and fern over the lip, scree, moss and litter only in a strip at the wall's foot, and dark leaf litter, mud, moss and fern on the floor, blended over the ground that was there so it fades into the hillside within a few metres. No trees, tree ferns, palms, cycads, horsetails or scrub from the floor's front to 6-9 m above the lip; ferns crowd the floor and come back over the lip. (The first version painted the cut face and a long apron as bare rock and scree: on the GPU with the real textures every notch read as a pale quarry 40-80 m across.)

The rock (`render/props_place.c`, `shoulder_layout`). Each wall is built from one of the two mossy rock sets (grey `rock_moss_set_02` or olive `rock_moss_set_01`, chosen per shoulder so a wall reads as one bed of rock). Those scans are closed (no open edge once their seams are welded), so a rock may stand on any side and nothing open shows from behind or above; they are scaled up to 6 times, lean back into the hill and are stood on whichever axis gives the shape a slot wants (never a slender piece on end). Three passes:
1. Along the wall's foot from one end to the other, wherever it stands over a metre: a rock as tall as the wall there (its top just over the lip, where the hill meets it), no wider than the wall's curve allows, a metre proud of the foot with its back sunk into the cut; the next one starts halfway back into this one, as the scans are rounded and their ends cover little.
2. At each joint, a rock lodged high in the seam with its back set into the wall, and most often a lower one in front, so the face reads as one irregular outcrop rather than a row.
3. A fill: the wall's points (ground rising into the hill steeper than 45 degrees, from the foot back to where the cut ends) seen from out in front of the mouth and from the hillsides on both sides, each rock taken as the ellipsoid inside its bounds; while a patch stays bare, a rock is lodged on it, sized to the patch.

The pieces are outcrop class: static convex colliders, never pushed about. Each belongs to the 64 m tile it stands in (so it streams in with the ground under it) and is laid out once per world when the props load (`prop_shoulders_prepare`, on threads: 0.07 s on 4 threads for the island), then read by both the colliders and the renderer. Other props keep off the rock: no cliff faces, outcrops, logs, stumps or roots of the island's own scatter on the wall, its top or (for the big ones) its floor; loose boulders and stones may lie on the floor.

How much of the wall the rock covers (`prop_shoulder_coverage`, `terrashade --coverage`): rays from the two review views (out in front of the mouth, and from the hill beside it: capture's `shoulder_N` and `shoulder_N_side`) to every point of the wall the ground in front does not hide, against the pieces' real meshes. On the island (seed 1, 2048^2): 60 shoulders (2.7 per km2 of land; the first version had 98), 3011 rocks in them (20 to 79 each); the rock covers 99% of the wall on average from both views (the lowest shoulder 94%), and the tallest run of bare wall in one place is under 1.5 m on 58 of the 60 (the other two: 1.7 m from the mouth and 2.5 m from the side at (5021, 5896), 1.6 m from the side at (3478, 1998)). The four capture shots: 98-99% from the mouth, 98-100% from the side, the tallest bare run 1.0 m. Without props (`assets/props` missing) the notches are still cut and painted.

Checks (`tests/test_sim.c`, `test_shoulders`, on the test island: 2 shoulders on 1 km2 of land): at least 2 and at most 12 per square kilometre, 60 m apart at least; every one has at least 3 m of wall in the middle and a dry floor; no tree trunk where the rock stands; no open edge of any piece above the drawn ground; the rock covers at least 80% of each wall from each review view with no bare run over 1.5 m (99%, 0.3 m); and no trap: a stegosaur put on each floor walks 15 m out of the mouth in 8 s.

Bots go round them. A notch's curved wall is a dead end for the bots' feel-ahead steering (every heading within 80 degrees meets rock). When the way to a bot's target crosses a wall (the bot on one side, the target on the other, within the wall's width), it heads for the nearer end of the wall first (`round_shoulder` in `bots.c`).

To look at one: `make -C ocean/cretaceous terrashade CC=clang && ./build/cretaceous/terrashade --site K --out FILE.jpg` (shoulder K, looking into it), `--coverage` (every shoulder's coverage), or with the renderer `capture DIR --shot 'shoulder_*'` (the four with the tallest walls, from the mouth, from the hill beside and from 200 m: `shoulder_N`, `shoulder_N_side`, `shoulder_N_far`).

Recommended defaults (terrain options need a world rebuild, so they are not in the Settings menu): `shoulders=0` (the bedrock shelves instead), `peaks=0.5`, `bedrock=1` and `ravine=2` (wide), as built.

## Ravines

`CRET_TERRA="ravine=N"` picks the ravines cut when the world loads (not baked, so switching costs no bake):
- `ravine=2`, wide (the default): a shallow valley about 9 m deep (7-11 with a noise) and about 85 m across (its half-width wanders 32-53 m), a flat floor of fern and dirt over its middle third and smooth banks no steeper than 31 degrees, so any animal climbs out anywhere; scree streaks where the banks are steepest, a thinner forest on them and an open floor with a few trees. No rock walls.
- `ravine=1`, stepped (the first version): 16 m deep, 30 m across, walls in three ledges of bare rock and scree lined with rock faces, a dry floor and no trees.
- `ravine=0`: none.

They run along the same winding lines in the same regions either way, keep 40 m from rivers and lakes, and leave rock shoulders whole (no cut within 5 m of a shoulder's reach, coming back over the next 25 m). `docs/terrain/ravine_*.jpg`: stepped on the left, wide on the right. The stepped gorges were narrow, walled-in and showed the black open backs of their wall slabs (left, below). Recommendation: keep the wide ravines. They add long, gentle valleys to travel and hunt along without trapping anything or showing broken rock; if the island reads better without them, `ravine=0` removes them. `tools/lookcheck.py`'s two gorge cameras now see the wide ravine and need approving again.

## The island's relief

Measured on the island the game bakes (seed 1, 2048^2 at 4 m: 8.2 km across, 21.8 km2 of land; `terrashade --relief`, gcc build as the owner builds; a clang build bakes a slightly different island, 609 m high before this branch):
- highest point 719 m at (4420, 3612) (632 m before the bedrock pass), in the middle of the island;
- summits (the highest point within 400 m): 718 m (4416, 3616), 616 m (5088, 3088), 576 m (4144, 5376), 560 m (2896, 2448), 325 m (6336, 4992), 305 m (5936, 4512); before: 631, 583, 540, 549, 497 (2368, 2560), 317 and 300 m. All lie inside the area animals and players spawn in;
- hypsometric curve, share of the land above each height: 100 m 66%, 200 m 43%, 300 m 24%, 400 m 10.8% (9.7% before), 500 m 3.6% (2.3%), 600 m 0.5% (0.1%);
- slopes: 72% of the land is steeper than 20 degrees, 52% than 30, 17% than 40 (15% before), 3.0% than 50 (1.5%);
- above about 380 m the mountains are bare: trees give way to scrub, scree and rock (Bedrock, above), so the summits read as mountain tops from the lowlands.

Why they did not read as mountains before this branch: since terra version 20 the slope cap makes every summit a broad dome, the forest went to the top (tree density 0.65-0.7 above 400 m) and bare rock was 1.5-2% of the ground above 400 m. The bedrock pass adds a tree line, exposure, crags on hard beds and more uplift under the high ground (`peaks`). Not done: a convergent range of 1000-1300 m peaks; on the 8 km island it would take about a quarter of the land, and a 3072^2 or 4096^2 island (a bake 2-4 times longer and a bigger web download) would keep the lowlands.

## Movable props

Boulders, stones of 28 cm radius or more, logs and driftwood are Box3D dynamic bodies (`dynprops.c`); smaller stones, roots, coast and cliff pieces stay static. Mass is volume × density (rock 2400 kg/m³, wood 420 kg/m³): on the test island boulders weigh 0.1–18 t (mean 1.8 t), stones 20–270 kg, logs 150–1740 kg, driftwood 70–510 kg. Props are seated on their footprint (the rim of the mesh's open bottom sits under the drawn cubic ground, `cret_height`; knife edges and overhangs are refused), and a moved prop draws a cap over that bottom. Movable ones take the lower of that seat and the seat on the bilinear Box3D heightfield they rest on when woken. Seated on the bilinear surface alone, 14% of rocks on the test island showed their open bottoms where the cubic dips between samples (41 by over 30 cm); now none do. Trees are moved onto the same drawn surface when the view hands over its coefficients (`cret_veg_set_surface`).

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
- **At the reticle** (the dot where the primary blow lands, shown while an animal is near enough to be hit): the primary's cooldown is a ring that closes as it runs out; a click waiting in the queue lights the dot up; a click the game threw away (more than 0.3 s early) flashes the ring red, so a refused click never looks like a lost one.
- **A whiff**: when your blow lands on nothing (judged 0.2 s after it was due, so a client's hits have arrived), an open grey ring widens on the reticle and a quiet whoosh plays. `capture --shot dino_feel_whiff` renders one.
- Settings has Camera shake and Hit-stop, each Off or 25-100%.

`capture --shot 'dino_feel_*'` renders a bite with the game HUD, a tyrannosaur's bite on the player, a tail swipe and a whiff, and prints the effect counts (`CRET_FX_DEBUG=1` adds the effect joints' world positions).

## Knockdowns

A heavy blow knocks a smaller animal off its feet for a moment (`damage()` in `sim.c`; constants `CRET_KD_*` in `sim.h`).

Who knocks down whom. The blow's strength (the attacker's share of the two masses x the attack's knock x the ability's) must reach 0.7, and the attacker must clearly outweigh the victim: 1.1 times for a blunt blow (a tail swing, a headbutt, a charge, a shoulder bash), 10 times for a side swipe of the tail (the secondary attack of an animal that fights head first: tyrannosaur, pachycephalosaur, sauropelta), 2.5 times for a bite or a kick. Never in water and never while it is already down. The rule reads the strength, not the knockback it becomes (2.2 x the strength, at most 4.5 m/s since the shove was retuned in a3281ad), so retuning the shove does not move who knocks down whom; 0.7 and the 1.8 at which it lies longest are the old 3.5 and 9 m/s divided by the old factor 5.
- A stegosaur's tail or sweep, a sauropelta's tail swipe or bash and a pachycephalosaur's headbutt or charge knock down a raptor or a gallimimus.
- A tyrannosaur's bite knocks down anything up to a stegosaur at growth 0.86 (3.2 t): sauropeltas, pachycephalosaurs, gallimimus, raptors and young stegosaurs. A grown stegosaur stays up, under its tail swipe too (1.6 times its weight). The tyrannosaur's and sauropelta's swipes floor raptors and gallimimus.
- A raptor never knocks down anything heavier than itself. A gallimimus's kick can floor a raptor (it weighs five times as much).

Timings. The animal lies 0.5 to 1.0 s (the fall included; longer for a harder blow: 0.63 s after a stegosaur's tail, 1.0 s after a headbutt), gets up over 0.4 s, then cannot be knocked down again for 4 s, so two attackers taking turns keep it down at most 26% of the time (measured: 20% with two stegosaurs swinging every 0.5 s). Lying it cannot move, turn, attack, eat, drink, call or use abilities; the knockback slides it along until it stops. Getting up it can turn but not move or attack. A knockdown drops what it carries and lets go of a latch. The fall does no damage and a downed animal takes no extra damage.
- The times are the branch's (cret/knockdown). Measured on 5.0 (docs/testing.md, Knockdown balance): every pairing stays within the run-to-run spread of 5.0. The 10x rule for side swipes is what keeps it so: with swipes at 1.1x like other blunt blows, a pachycephalosaur fleeing a raptor pack floored a pursuer with nearly every swipe (11 knockdowns a fight) and the pack won 1-2% of hostile fights instead of 85%; with it, 82% (game AI 53%, as on 5.0).
- Observations (contract 5, unchanged size 243): a knocked-down animal reads as stunned. Its own stun feature (the first contract 4 self float, `CRET_OBS_V3`) holds the longer of the stun and the time left lying and getting up, over 2 s; the neighbour stunned flag (the first of the 4 contract 4 floats per neighbour) is set while it is down or getting up. Abilities read not ready.
- Counted in `stats.knockdowns[attacker][victim]` (`balance` prints the table) and logged as a `CRET_HIT_DOWN` entry right after the blow, which `fx.c` and scenario summaries read (it is not a hit: no blood, no hit marker).
- Network (protocol 19): the knockdown rides in the snapshot (when it is up again, the fall's length and side) and in the own state; the predicted local animal stops when the server has it down (docs/net.md). Lag compensation needs nothing more: the sim never moves or reshapes a lying animal's capsule (the roll is drawn, not simulated), so its pose history is its real pose and the rewound hit test reads it as it reads a standing one.
- Reward: a knockdown pays nothing of its own. The blow that causes it pays its damage at `cret_reward_worth`, exactly as a blow that does not.
- Bots (`bots.c`): a hunter whose prey is down charges at once if it is within three charge distances (as when a packmate charges) and sprints while the prey is down; a downed bot does nothing until it is up. Defending herbivores already face the predator and strike in reach. Two more rules were tried and dropped because they shifted fight results (docs/testing.md): a predator that does not break off while its prey is down, and a hurt herbivore that flees while the predator is down.

What you see (`render/dino.c`, `render/view.c`, `render/fx.c`, `hud.c`):
- The whole body rolls about its long axis onto the side the blow pushed it toward, over 0.35 s like a topple, with one small rebound. It lies with the legs paddling slowly (the walk clip, half in), then rolls back up over the get-up. This goes on top of whatever clip plays, and foot IK is off while it lies. Capsules (no models) tip over the same way and sink onto their flank (`cret_dino_down_roll`, shared).
- Dust puffs out along that flank when the body lands. When it is you that falls, the camera shakes a little and the HUD shows "Knocked down 0.8 s", then "Getting up - you can turn"; the ability buttons grey out.
- To look at it: `./build/cretaceous/cretaceous play scenario knockdown` (or `capture DIR --scenario knockdown`). A raptor that cannot die goes for a stegosaur, whose tail floors it about every 6 s (5 knockdowns in 30 s). Check that the roll goes away from the stegosaur, that the dust is at the flank it lands on, that the body does not sink into the ground or float while lying (the lift is 0.3 x the model's width, `render/dino.c`), and that the legs and head look right lying down. Then take over the raptor with M to see the HUD chip and the shake. Nothing could be rendered in the sandbox, so these numbers are untested on screen.

## Wounds

A hard blow leaves a wound that outlasts the bleeding (`wounds.c`, called from `damage()` in `sim.c`; constants `CRET_WOUND_*` in `sim.h`, one comment each). A hunt can be: wound it, follow it, finish it.

Where and how bad. A blow of at least 8% of the victim's max health (after armour, `CRET_WOUND_MIN`) wounds the place it came at, from the nearest point of the attacker's body (so a tail swing comes from the tail):
- from in front (within 50 degrees of the victim's heading): the head; from behind (beyond 130 degrees): the hind legs; from the side: the body;
- an attacker under 0.6 of the victim's height goes for the legs from anywhere (a raptor on a stegosaur), one over 1.6 times its height bites down on the body (a tyrannosaur on a raptor); the raptor's hamstring bite always wounds a leg.
- Severity is the blow's share over 0.4 (`CRET_WOUND_FULL`), at most 1, and blows on the same place add up. One wound per place, so at most three per animal: a severity and the tick of the last blow each (`CretAgent.wound`). No random numbers: the sim's stream is unchanged.
- Why 8%. From the species table, the share one adult blow takes: a raptor's bite 22% of a raptor, 16% of a gallimimus, 7% of a pachycephalosaur, 3% of a stegosaur, 2% of a sauropelta, 1.3% of a tyrannosaur; its slash 9% of a raptor; a stegosaur's tail 50% of a raptor and 3% of a tyrannosaur; a pachycephalosaur's or a sauropelta's blow 25% of a raptor; a gallimimus's kick 10% of a raptor; a tyrannosaur's bite 27% of a stegosaur or a sauropelta and more than the health of the rest. 8% keeps the nibbling of a pack on a big animal and a raptor's bite on a grown pachycephalosaur as bleeding only, and makes every blow that matters on its own a wound. In `combatprobe` those are the blows that land 82-100% of the time (raptor on gallimimus, raptor duels, a stegosaur's tail on raptors, every tyrannosaur bite), so a fight that decides anything leaves one.

Healing. Per game day (scaled by `day_seconds`): 1 fed and watered and resting, 0.5 fed and awake, 0.15 hungry or thirsty (food or water under 0.3); it worsens by 0.4 a day starving (gut empty and fat under a tenth) or parched. A severity-1 wound lasts a day lying up, two on the move, a week hungry.

Effects, at severity 1 (linear in it):
- leg: top speed and turn rate 30% lower (a raptor trots 3.3 instead of 4.6 m/s); limps on screen from 0.2;
- body: bleeds 0.06% of max health a second (3.6% a minute), under a third of a fed animal's healing and under the open-wound line (`CRET_WOUND_OPEN`), so it neither stops healing nor credits a later death to the attacker; stamina comes back 40% slower;
- head: smell, sight and hearing 40% shorter (`cret_scent_ranges`, `cret_eco_sight_m`, hearing in `cret_eco_perceive`, and the critter senses in `scent.c`).

Blood (`scent.c`, docs/progression.md Scent). A bleeding animal (an open bleed or a body wound of 0.25 or more) drips a blood mark on every second point of its trail (every 3 m); one under half its health or with any wound of 0.25 or more every third of that. Marks last 240 s against 45 s for the rest of a trail, in a ring of 512 for the island (was 192). With nobody about (the trainer, the probes) only animals that bleed keep a trail, so bots have blood to follow and nothing else is paid for.

Bots (`bots.c`, `eco.c`; docs/ecosystem.md sections 5 and 6):
- predators prefer the weak through the hunt arithmetic: a leg wound enters the prey's speed (`eco_top_speed`), so a limping animal is easier to catch;
- a hungry predator also ranks the wounded herbivores whose blood it smells but no longer knows of, by the same rate as the prey it knows (`blood_prey`); it follows the blood from mark to mark until it senses the animal again, and gives up when it loses the trail;
- a herd keeps to a walk for 0.15 of a game day (3 minutes at the default day) for a member that limps from a fresh leg wound, then goes at its own pace and leaves it (`cret_herd_waits`).

Players and policies:
- the HUD lists the wounds with the injuries ("Bleeding, bad leg wound, head wound"); the sniff panel calls a wounded animal's trail a blood trail, in red (`hunt_hud.c`); the limp is the broken-bone gait (`render/dino.c`);
- observations (contract 6): the own leg, body and head severity, three floats appended after the 243 of contract 5 (246); the reward is unchanged (a body wound's bleeding counts as damage taken, as all bleeding does);
- network (protocol 21): each agent's three severities in twentieths in the snapshot (the limp and the blood a client draws), the own ones exactly (prediction slows the animal by its leg wound; docs/net.md);
- the scenario `wounds` (`play scenario wounds`, or `capture DIR --scenario wounds`) shows a gallimimus with a bad leg wound and a body wound limping away from its herd. Check that it lurches (the broken-bone gait) and drips; take over the raptor with M and hold V: red marks along its way, and "Gallimimus blood trail" in red in the scent panel. Nothing could be rendered in the sandbox (no GPU, no dinosaur models): the render code only compiles.

Measured (cret/wounds, with the real props; docs/testing.md, Wounds, has the tables). On the island (`ecoprobe --only world --days 6`, 48 animals, seeds 1-3, 4-6 and 7-12, 72 seed-days):
- wounds per animal-day: raptor 1.36 (mostly the body: stegosaur tails, sauropelta bashes, pachycephalosaur butts), gallimimus 1.05 (legs: raptors bite from behind), stegosaur 0.76 (legs), sauropelta 0.76 (body: tyrannosaurs bite down), pachycephalosaur 0.32, tyrannosaur 0.01;
- bot hunts begun on a blood trail: 91 (77 by raptors, 14 by tyrannosaurs), of the 1,796 bot hunts in those runs (1,135 by raptors, 661 by tyrannosaurs; 1,600 before); 3 ended with the prey dead. Wounded prey are mostly found by sight (the leg wound in the hunt arithmetic) before the blood matters;
- deaths per animal-day, before and after, pooled: raptor 0.40 / 0.41, stegosaur 0.16 / 0.19, tyrannosaur 0.07 / 0.07, pachycephalosaur 0.16 / 0.11, sauropelta 0.11 / 0.17, gallimimus 0.17 / 0.22. Raptors kill 76 gallimimus instead of 55 and 44 pachycephalosaurs instead of 56, tyrannosaurs 115 stegosaurs instead of 86 and 55 sauropeltas instead of 44; gallimimus flee 9.8% of the day instead of 5.2%. Hunt success: raptor 18% / 16%, tyrannosaur 32% / 25% (more hunts, 3.3 / 3.6 a day). The three seed sets of the same code differ by up to 2.5x in a cell, so only the pooled moves mean anything;
- `combatprobe`: a fight ends sooner once the loser limps (tyrannosaur on stegosaur 18.2 → 12.3 s to kill, raptor duel 25.3 → 22.5 s, tyrannosaur duel 31.2 → 25.1 s); hit rates unchanged within 1-5 points;
- cost: bots 0.041-0.054 → 0.050-0.059 ms a tick on the island (three runs each), `bench` unchanged within its spread.

## Nests, eggs and carrying food

Nests (`progress.c`, `docs/progression.md`) are drawn by `render/nest.c`:
- **The nest**: a procedural ground nest built on the terrain itself (`cret_height`), so it follows a slope and never floats or sinks. It is a low mound of soil with a shallow bowl, its rim a sixth of the radius high, and sticks laid round the rim, so it reads as a nest from a distance and up close. The radius goes with the species' length (`cret_nest_radius`): 2.3 m across for a raptor, 3.2 m for a gallimimus, 5 m for a tyrannosaur. Each nest has its own shape (rim wobble, lumps and stick layout from its position). Plants are not drawn inside it (`cret_veg_set_clearings`), as if the builder had cleared the spot.
- **Look**: `shaders/nest.fs` lights it as the terrain is lit: sun with the cascaded shadows, sky ambient, ground bounce, contact shadows under animals, rain darkening, and no specular. The ground photos (dirt, forest floor, fern sward) give the detail. Each part takes its hue from a base colour: soil, leaf and fern litter lying in drifts with soil between, dark and weathered sticks. The mound and the eggs cast shadows in the two nearest cascades.
- **Eggs**: as many as the sim's count (5 at most); they disappear as they hatch or are eaten. Each species has its own size and shape: elongated theropod eggs (raptor 17 x 7.5 cm, tyrannosaur 42 x 17 cm, gallimimus 15 x 6.5 cm) in a ring with the narrow ends inward and down; rounder ornithischian eggs (stegosaur 22 x 18, pachycephalosaur 15 x 12, sauropelta 20 x 17 cm) in a heap. Each egg is a real egg shape with exact normals. The shells are matte (the nest shader, no specular) and darker underneath.
- **Building**: while an animal holds N, the nest rises where it will stand: the mound grows from the ground and the sticks arrive as the 5 s pass. Clients get no build progress for other animals, so their nests rise on the client's own clock.
- **Loose and carried food**: eggs and caught critters that are carcass slots (below) lie on the ground, or hang crosswise in a carrier's jaws, at the model's head joint (the mouth point for capsules). A carried dinosaur carcass is drawn by `render/dino.c` at the mouth.
- The HUD's nest marker sits well above the nest (higher for bigger nests) and fades once you are close, so it never hides the nest.
- `capture DIR --shot 'nest_*'` stages nests on a 20 degree hillside: a raptor nest with five eggs, a tyrannosaur nest with three, a hatched gallimimus nest, a tyrannosaur halfway through building and a raptor carrying an egg away. `capture DIR --nest-smoke` (`make nest-smoke`) is the headless drawing check.

Carrying (`sim.c`, the carrying section; `docs/progression.md`):
- Carnivores press B (`CretInput.carry`, a one-tick press) to pick up what lies at the mouth: a carcass whose weight is at most a quarter of their mass (a body weighs twice its meat), the rest of a critter they caught but were too full to finish, or an egg from another group's nest (a raid, 8 kg). Press B again to put it down.
- A raptor (80 kg) lifts 20 kg: eggs and catches. A tyrannosaur (8 t) lifts 2 t: a gallimimus or a young stegosaur.
- Carrying slows the animal by 0.8 x the load ratio (a full load, 20%). It blocks biting (the primary attack unless it is a tail, and the special bites and the pounce), eating and drinking. A blow of 8% of max health or one that throws the carrier, a stun or death makes it drop the load.
- A carried item stays a carcass slot: it keeps rotting, nobody can eat from it while it is held, and once it is put down anyone can eat from it, the young at the nest included.
- Bots: a fed predator parent whose group has a young member near the nest sometimes (about half the kills, by slot and carcass, so it stays deterministic) carries a small kill it can lift home, puts it down at the nest and stays a while. Grown animals that are not starving leave food at their own nest to the young.

## Small prey and the early game

A new player spawns as a hatchling, and nearly everything alive is bigger than it. Before this layer a hatchling raptor could eat only carcasses and eggs, starved in 15 minutes walking, and its quests were 300-400 m away. Real ecosystems are mostly small animals: population density goes roughly with body mass^-0.75 (Damuth's law), juvenile tyrannosaurs ate small dinosaurs (a juvenile Gorgosaurus has two yearling dinosaurs in its stomach), and mammals such as Repenomamus (about 12 kg) even ate baby dinosaurs.

**Critters** (`critters.[ch]`) are not agents: no Box3D bodies, no RL slots, nothing in the observations (`CRET_OBS_SIZE` and the contract are unchanged). They are a pool of 384 small structs (position, heading, kind, state, mass, bites left) standing on the terrain, kept in `CretSim` and stepped at the end of `cret_sim_step` with the sim rng, so the sim stays deterministic. They exist only around people (`cret_is_person`): the trainer has none, so training never runs them.

| Kind | Mass | Lives | Does |
|---|---|---|---|
| Multituberculate (small mammal) | 0.3-2 kg | thick plants | skitters off at 4.6 m/s, then freezes in the undergrowth for 5-9 s |
| Repenomamus (large mammal) | 8-14 kg | plants or woods | stands and bites animals under 4x its mass, and anything that corners it: 2.5 hp and a little bleeding a bite (about 9% of a hatchling raptor, 2% of an adult, nothing to a tyrannosaur); 2.5 bites to kill (three from a hatchling raptor, two from an adult, one from anything of 160 kg) |
| Lizard | 0.2-2 kg | anywhere dry, more in plants | fast bursts (5.5 m/s for half a second), pauses, bursts again |
| Small bird | 0.1-1 kg | open ground and trees | flushes and flies off; gone |
| Fish | 1-10 kg | river and lake shallows (0.15-2 m) | caught only by an animal standing in the water |

- **Where**: each person keeps up to 80 around it, spawned 25-120 m out (never closer to anyone) and gone past 160 m from everyone. A person who spawned as a hatchling (growth under 0.5) gets up to 128, 12-70 m out, for its first 4 minutes, with mice, lizards and fish favoured over birds. A spawn try picks a kind by density x habitat and is kept with the total over its best case, so lush ground fills and bare rock stays nearly empty. Habitat comes from the world data: plant cover (`cret_forage`) for mammals and lizards, open ground and trees for birds, fresh shallows for fish. Densities are Damuth's law over each kind's mean mass, times `config/critters.ini`.
- **Behaviour**: they wander slowly and flee from animals much heavier than they are (2-4x their mass). They notice an animal by the largest of three senses: **hearing** (the detection range, 5-12 m, x (speed / 4.5 m/s)², so a sprint is heard at the full range and a creep hardly at all), **sight** (0.45 of the range, a third of that for an animal standing still, plus half its height, but only 0.3 of it while the critter feeds with its nose down) and **smell** (mammals and lizards, in the plume downwind of the animal: a stalker downwind is not smelled, one upwind is, at up to 1.5x the range). Each feeding critter looks up on a cycle of its own (`cret_critter_watch`: a mouse 1.2 s up, 2.8 s down), and its head takes 0.3 s to come up, so a stalker who freezes when it lifts is not seen. They react after 0.15-0.4 s, jink 60-90 degrees when the chaser is within 2.5 m, and a mouse or lizard still chased hard after two flee legs dives into a burrow and is gone. A hiding mouse sees little but bolts again at a run. They steer round ground they cannot stand on with a few walkability checks (water for fish), and use no physics queries. The large mammal faces small animals it watches and fights when they come within 3 m.
- **Eating**: the primary attack catches a critter at the strike point, with the same reach as a bite on an animal (`resolve_attack`: whichever is nearer the jaws), and a quick snap 0.13 s into the bite catches one within 1.25x that reach (`resolve_snap`). Small ones die in one bite and go straight into the stomach (no carcass); the large mammal takes 2-3. Food is the carcass formula, `kg / (0.25 x body mass)`, capped at full and credited to `ev_food`, so a 2 kg mouse fills a hatchling raptor (3.4 kg), gives an adult raptor a tenth of its stomach and an adult tyrannosaur 0.1%. Only carnivores gain food (a herbivore's bite kills one for nothing).
- **HUD** (`hunt_hud.c`): "Left mouse  bite the small prey" with one in reach. Otherwise the nearest critter the animal has noticed (seen in front, heard close by, or smelled; `cret_prey_noticed`) within 30 m, named with where it is from the camera's view: "Lizard ahead to the left, 9 m  -  it is feeding: creep closer" (or "it is looking round: keep still" within 12 m). A tick on the compass and a small chevron over it (an arrow at the screen edge when it is off screen) lead to it; the catch quest's tracker says how far and which way. With nothing noticed: "Hold V to sniff for small prey".
- **Bots**: a hungry carnivore bot below growth 0.55 with nothing better to do (wandering, resting or grazing, so no prey it would take) stalks the nearest mammal or lizard within 45 m: if it stands in the prey's smell it first circles wide (15 m to the side) to a point downwind of it, then walks to 9 m, creeps while the prey feeds, stands still while it looks up (except within 2.2 m, where it is seen anyway and goes in to snap) and chases it if it bolts (`cret_critter_bot`, one call in `cret_bot_input`).
- **Network**: the server is authoritative. Each snapshot carries the client's 24 nearest critters within 80 m (protocol v12, at most 177 bytes; docs/net.md); catches resolve on the server.
- **Drawing**: `render/critters.c` draws each from a procedural mesh (`render/critter_mesh.h`, three LODs) posed in `shaders/critter.vs` without bones, within 70 m of the camera. The head follows the sim's watch cycle (nose down sniffing, pecking or feeding; up and turning while it watches), every other small mammal sits up on its hind legs to look round, mice and birds hop while they feed, lizards flick their tails. Colours are countershaded with a pattern per kind (a dark spine line and cream belly on the mouse, a pale blaze on Repenomamus, a rust spine stripe, cream side stripes and a slate-blue tail on the lizard, a rufous breast and pale brow on the bird), and the eyes catch the sky and the sun. Critters within 25 m add to the contact-shadow list in the slots the animals leave (`view.c set_contacts`), which also stamps the trample map: a critter moving through ferns or grass parts them. Sky light is dimmed by the ground cover where each stands, as the cover's own is. Positions are eased per pool slot, so a client's 15 Hz updates do not step.
- **Cost**: about 0.008-0.011 ms a tick for 100-128 critters with 48 animals (`test_critters`, `tools/critprobe.c`), against a target of 0.1 ms.

**Hunting** (`scent.[ch]`, docs/progression.md has the numbers): hold V (a touch Sniff button) as a carnivore to sniff. The animal creeps and cannot sprint, and it is no harder to notice. Every critter and animal near a person leaves a trail of its last 8 places (recorded by the sim from positions it already has, no randomness; clients record theirs from snapshots), and smells drift downwind (`cret_wind`, the same formula as the plant wind). A source is smelled within the species' range times a wind lobe (1.25 straight downwind of it, 0.55 across the wind, 0.1 upwind) and fades over 45 s; rain shortens both. A raptor smells small prey nearby (28 m as an adult, 18 m as a hatchling), carcasses at 140 m and other animals' trails at 70 m; a tyrannosaur carrion at 450 m; herbivores do not hunt by smell. The scent view (`render/scentfx.c`, after post-processing so it reads the same day and night) draws faint wisps along trails that reach the nose, drifting and fading with age (warm for prey, grey-green for dinosaurs), a plume over carcasses and a soft expanding pulse on the nearest prey and carcass, dimmed but seen through plants; another dinosaur shows only as its trail, at least 3 s behind it, and the panel names its age and heading. Not sniffing, a carnivore still smells at 0.35 of the range, which is what lets the HUD point at prey upwind that it cannot see.

`tools/catchprobe.c` (`make catchprobe`) measures catching: a hatchling raptor against one critter, from downwind, across the wind or upwind, careful, walking or sprinting, at 15 and 60 fps. A careful stalk from downwind catches a mouse 75-80% of the time and a lizard 95%; a sprint straight at it 10-17% and 0% (before: every mouse caught by any approach once it froze, no lizard ever) (docs/progression.md).

`capture DIR --hunt` (`huntcap.c`) stands a hatchling raptor in ferns, grass, leaf litter, on sand and by fresh shallows on the full island and photographs each critter kind 3, 8 and 15 m ahead at noon, dusk and night from the game camera, measuring each still against a ring of background (`DIR/hunt_table.csv`, `DIR/hunt_summary.md`). `CRET_HUNT_ONLY=sand,noon` picks stills, `CRET_HUNT_HUD=1` adds the HUD and `CRET_HUNT_SNIFF=1` the scent view.

**Early-game pacing** (`progress.c`): a person below growth 0.5 gets short errands within 150 m instead of named places: drink at the nearest water when thirsty, else catch three small prey (predators) or graze (herbivores) at the best of 24 spots 60-140 m away, for 15 marks and 0.04 growth. A person who comes back as a hatchling drops its old errand for a near one. Every other quest picks places about `150 + 250 x (growth - 0.35) / 0.45` m away (150 m for a hatchling, the old 400 m from growth 0.8), and animals below growth 0.8 turn down a place more than three times that far. Hunger and thirst rates are unchanged.

`tools/critprobe.c` plays a scripted hatchling on the full island after a minute of title screen, with the population director on: a predator stalks and bites critters within 30 m when it could eat and otherwise trots to its quest; a grazer grazes where plants are thick. 4 sessions of 15 minutes each:

| Hatchling | Critters | First meal | First quest | Food by minute 5 / 10 / 15 (mean) | Time with an empty stomach | Deaths (all from bots) |
|---|---|---|---|---|---|---|
| Velociraptor | on | 17 s | 89 m, reached or done in 46 s, 4 of 4 | 0.94 / 0.90 / 0.63 | 0 s | 3 (all in one session, to herbivores defending) |
| Velociraptor | off | 412 s (2 of 4; 2 never ate) | 342 m, 1 of 4 | 0.62 / 0.56 / 0.42 | 114 s | 2 |
| Stegosaurus | on | 14 s | 110 m, 4 of 4 | 0.90 / 0.60 / 0.72 | 0 s | 4 |
| Stegosaurus | off | 3 s | 89 m, 3 of 4 | 0.95 / 0.72 / 0.75 | 0 s | 5 |

"Off" keeps the new quest ranges (a hatchling raptor has no catch quest then, so it gets ordinary quests, scaled down). The raptor hatchling caught 23 mice, 25 lizards and 9 Repenomamus over 4 sessions, no birds and no fish (few shallows lay on its paths). Grazers were never short of food; with or without critters, bots kill hatchlings (mostly tyrannosaurs and raptors on stegosaurs; herds defending on raptors).

```bash
make -C ocean/cretaceous critprobe && ./build/cretaceous/critprobe --species raptor     # --critters 0 compares the game without them
```

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

## Clouds

`shaders/clouds.fs` marches a cumulus layer between 1300 and 4600 m (`CLOUD_BASE`, `CLOUD_TOP` in `common.glsl`) the way production volumetric skies do (Schneider 2015, Hillaire 2016):
- **Weather map** (`make_cloud_noise`, `render/view.c`): a 512² tile over 30 km, drifting with the wind. R is a continuous coverage field, not one dome per cloud: cumulus clumps of every size (5-octave fbm plus a Worley billow term) inside broad fields with clear lanes, rowed along x into cloud streets by an anisotropic term. G is the cloud type (0 flat stratocumulus, 0.5 cumulus, 1 towering): the middle of big dense fields towers, stragglers stay flat. B masks the high clouds. A lifts each field's base by up to 700 m (`CLOUD_LIFT`), so bases do not all sit on one plane and far fields do not line up on the horizon. The field drifts at about 15 m/s (`cloud_wind`, `common.glsl`; the ground shadows use the same). The cover slider remaps R (clear below `1 - 0.75 cover`, solid 0.45 above that), so more cover adds clouds and fattens them; above about 0.7 the field closes into an overcast deck (`u_overcast`).
- **Shape**: the type picks a height profile (a base that thins over its lowest sixth, coverage falling toward the top), the height falls to a few percent at a cloud's rim so its sides round over instead of ending in a wall, the Perlin-Worley base shape (128³, `cloud_shape_v2.bin`) is carved by the coverage remap, and Worley detail (64³, `cloud_detail_v3.bin`), pushed around by a curl field from the tiling noise, erodes the surface: wisps at the base, cauliflower billows above. Erosion bites where the base density is thin, so the interior stays solid.
- **Lighting**: a sun march with growing steps (reused every other sample), Beer-powder, three multiple-scattering octaves (the forward lobe gives silver linings), sun through the low air (golden hour reddens the clouds), sky light graded from the bright top to the blue-grey base, aerial perspective toward the horizon. Moonlit clouds are dimmed to silver-grey.
- **Thin layers** (`thin_layer`): an altocumulus deck at 5.6 km (a mackerel pattern from the shape noise, closing with the cover) and cirrus wisps at 9.5 km (combed along the wind, present under fair and mixed skies), both planes, lit by the sun at their altitude plus sky light.
- **Shadows on the ground** (`cloud_shadow`, `shadow.glsl`) read the same coverage field with the same remap.

The cloud buffer is a **full-resolution sky image** kept across frames (`cloud_resolve.fs`). Each frame marches one pixel of every 4x4 block (a 16-frame Bayer order, blue-noise jittered steps, finer steps at the cloud entry) into a quarter-size target; the resolve reprojects the history by view direction (from the camera basis, so a still view reads its own texel back exactly; a turn is exact for clouds at infinity), blends fresh samples in at 40%, limits only the brightness of a fresh sample against cloud of the same opacity next door, and under motion (Catmull-Rom resampling) keeps the history unless it is far outside what the marched neighbours show. A pixel without history takes a tent over the 4x4 marched samples around it. Clamping tightly to the neighbours, or taking the four nearest alone, drew cloud edges in 4-pixel steps whenever the camera turned, because each marched sample is a single jittered ray. Pixels whose surface hides the whole layer are skipped by the march and keep the sky image behind them, so nothing is missing when a tree moves out of the way; the composite (`post.fs`, and the bloom and exposure passes) applies that occlusion with the full-res depth. A reset (teleport, resize) shows the bilinear estimate of the sparse samples and sharpens over a few 16-frame cycles.

Cost on a GTX 1060 at 1080p, high preset: 1.0 to 2.0 ms for the march (open sky to a backlit field at dawn) plus 0.4 ms for the resolve. Medium and low lower the render scale and the step counts (`cloud_quality`).

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
- **Leaves and fronds** (`veg_light.glsl`) have no specular lobe. A card or strip is flat, so any highlight lights the whole strip at once and reads as plastic.
  - Normals are bent outward from the crown, and lit with a squared half-Lambert, so a crown shades as one soft volume.
  - Across each card the normals round outward (26 degrees at its edges), so a leaf, frond or blade falls off in light across its width like a stem instead of being one flat value. Frond leaflets and grass and sward blades on the full meshes also fold along a centre crease, so edge on they show a thin wedge.
  - A leaf card turning edge on erodes away before it becomes a line, each card at its own angle; the depth prepass cuts the same and shadows are unchanged.
  - Sun shining through a blade glows yellow-green when you see the blade's face away from the sun (judged by the card's own flat normal).
  - [docs/plants](docs/plants/README.md) has the audit (texel density, edge-on share), the switches (`CRET_VEG_ROUND`, `CRET_VEG_CREASE`, `CRET_VEG_EDGE`), the cost, and what was tried and dropped.
  - Sky and ground light come from a hemisphere, scaled by the baked crown occlusion and filtered green through the crown.
  - Each leaf varies its value by ±12%, and ground cover darkens toward its base.
  - Rain adds only a faint, broad sheen of grey sky.
- **Rock, wood and bark** (`props.fs`) keep a dim, broad lobe: roughness at least 0.7 when dry and about 0.45 under rain, with roughness-limited Fresnel, plus a faint wet sheen in rain.
- **Dinosaur skin** (`dino.fs`) gets a soft wrap and a faint rough Fresnel rim. Its lobe has roughness at least 0.65, or 0.35 when wet (in rain or below the waterline).
- **Ground** (`terrain.fs`) has roughness at least 0.75, lowered only in the damp band at the water's edge and in rain.
- **Far props** (`props.fs`): roughness rises to 0.92 between 25 and 150 m, as the normal map's relief averages out in its mips, so a distant rock or log does not keep the sheen of a smooth one.

**The ground at distance** (`terrain.fs`, `render/view.c` `build_terrain` and `build_canopy`):
- Tiling: a second copy of each planar tile at the same scale, rotated 37 degrees and read through a smooth warp (about 600 m across, up to 10 m), is mixed in by a noise mask a few metres across. A copy magnified 3.4x did this before; on the litter photos it blew twigs up into metre-long strokes that read as brush marks at 50-150 m. Detail normals fade out between 60 and 300 m. The noise texture repeats every few metres at the scale it is read at, so its larger lookups (the anti-tiling mask, the border raggedness at 9 m, the clump mottle at 13 m) average two reads at incommensurate periods on rotated axes (one read on low).
- Landform: `t_cover` (half map resolution, derived from the bake at load and cached) carries understorey density (ferns, horsetails, scrub, cycads), its woody share, drainage wetness (log flow) and curvature. Hollows and drainage lines are darker, greener and smoother; spurs and ridges paler and rougher.
- Understorey: the ground-cover meshes end at their range (90 m on high) and dither out over its last 30%. From 45% of that range the terrain takes on their look where `t_cover` says plants grow: a dark green sward (olive where scrub dominates), clumped at 3 and 11 m with its own shading normal, matte, hiding part of its floor from the sky and from a low sun.
- Canopy: `t_canopy` holds the far canopy colour of each stand and the share of ground under the drawn crowns (`cret_veg_crown_cover`, from the tree instances, not the bake's canopy density). Past the shadow cascades that share shades the forest floor, more with a low sun; between distant crowns and past the impostor range it paints the forest.
- Close to the camera the fern ground's green recolour is eased (45% at 3 m, full by 25 m), so the litter under the drawn ferns stays brown with its moss.
- `CRET_SPLAT_DEBUG=2` shows `t_cover` (understorey red, woody green, wetness blue), `=3` its curvature.

**Shadows and dithers.** No screen pattern is left in a still frame: the shadow filter's disk is the same for every pixel (it was rotated per pixel by interleaved gradient noise, a pattern made for a temporal filter this renderer does not have, and left a fixed crosshatch in every penumbra), and impostors blend their two nearest baked views instead of choosing one per pixel. Only LOD crossfades still dither, while they run, and on the low preset impostors still pick one view per pixel (blending costs a second set of lookups).

**Upscaling.** Below render scale 1 the scene is resampled to the window with Catmull-Rom (nine bilinear taps, clamped to the four nearest texels against ringing) instead of one bilinear tap, except on the low preset, which keeps the one tap for its frame time. At render scale 1 nothing in the post chain blurs: there is no temporal filter or depth of field, bloom adds 6% of a quarter-size blur, and the haze within 100 m is under 5%. On high and ultra at render scale 1 a light sharpen (`post.fs` `scene_sharpened`, `CRET_SHARPEN`) pushes each pixel's luma away from its four neighbours, less across strong edges, clamped to their range so no halo forms; it gives back the contrast that texture filtering and alpha-tested foliage soften.

**Foliage colour** comes from the atlas, not the lighting. Photo leaves are evenly lit and pale, so `grade_layer` (`render/veg.c`) grades every leaf layer once at startup:
- it stretches the leaf-to-leaf value contrast around the layer's mean;
- it darkens toward the rachis and the frond base (deep blue-green) and warms the tips (yellow-green);
- it browns a few tips in patches;
- it settles the layer's mean a little deeper than its photo target.
The impostors are baked from the graded atlas. The terrain's fern sward stretches its photo detail the same way (`RECOLOR_CONTRAST` in `terrain.fs`).
Bark photos are scaled to a species tone, and values above the photo's mean are compressed and pulled toward its hue, so pale flakes and lichen (the metasequoia photo's white flecks) stay lighter bark instead of birch-like white blotches. Each species has its own bark scan (Trunks and bark, above).

**Umbrella araucaria** (`VEG_CONIFER` variants 2 and 6, Araucaria araucana-like): a bare trunk with dead branch stubs to about 62% of its height under a dome. Branch length follows a quarter circle up the crown (4.6 m at the bottom whorl, 1.5 m at the top), branches run out nearly level and turn their tips up, woody near the trunk and then a slim dark scale-leaf tube under a sleeve of crossed rope cards (the sawtooth outline of the scale leaves, which a smooth tube could not give) with rope-cluster branchlets curling up and out on alternate sides, and the whorls continue to the leader under a cap of rising rope clusters. The branches used to be 13 cm scale-textured tubes with sparse branchlets and read as rubber hose; the Norfolk's were thicker still (16 cm at the trunk, tapering slowly) with sparse cords, which is what looked like play-doh ropes. Both are now 4 to 9 cm poles, pencil-thin at the tip.

**Contact shadows** (`csm.glsl` `contact_occlusion`, `cret_csm_set_contacts`): the 16 nearest animals within 45 m are passed as ellipses (position, yaw, half length, half width, height).
Terrain, plants and props darken their sky light under and around each animal, so animals sit in the grass under an overcast sky and at night too. The sun shadow maps carry the direct shadow, and the contact shadows work with them off.

## Quality presets and level of detail

Presets (`render/view.c` `cret_view_set_preset`): low, medium and high as before, high sized for a mid-range GPU (a GTX 1060 at 1080p), and **ultra** above it for stronger GPUs: plant ranges 35% longer (full tree meshes to 43 m, impostors from 176 m, ground cover to 122 m, tall grass to 61 m), 40% more sward tufts and grass clumps, props 40% further, shadow maps at 3072², 16x anisotropy on the ground and props and finer far terrain. Clouds are the same as on high. [docs/quality](docs/quality/README.md) lists every preset's resolution, shadow texels, texture sizes and texel density on screen, triangles per level of detail, switch distances, the cost counts and before and after captures.

What high gained over 5.0, at about the same cost (draws -6 to -11%, triangles -3 to -7%, fragments -4 to +7%, `CRET_GL_STATS=1`):
- **Near shadows.** The cascades end at 14, 45, 180 and 800 m (were 20, 60, 200, 800): 1.6 cm texels in the first (2.3 before), 5.2 cm in the second (6.9); the smallest penumbra is 5 cm instead of 8 (`csm.glsl`), so fronds, stones and logs cast defined shadows at a hatchling's eye. Ground cover outside the smaller first cascade casts into the second, out to 44 m, so no shadow is lost.
- **Sharpen** at full resolution (above), and 8x anisotropy on the plant atlas (bark and leaves; 4x before). Anisotropy goes through `gfx_set_aniso`, which does nothing where the context lacks the extension (WebGL's `EXT_texture_filter_anisotropic`).

Level-of-detail changes, on every preset unless noted:
- Tree switch distances (reduced mesh, impostor) scale with the tree's size on screen (above, `tree_switch_scale`); the impostor fade is the complement of the mesh fade, so the crown keeps its coverage through the swap.
- Ground cover grows in from 70% of its size as it dithers in at the edge of its range; sward tufts crossfade to their reduced mesh across 8-10 m (they switched at 9 m in one frame).
- Props: every prop dithers out over the last 15% of its range (stones and debris vanished in one frame at 200 footprint radii, 16-90 m), and stones and debris crossfade between meshes like the other props.
- Shadow casters (high and ultra) take their mesh from the cascade: full meshes in the first, reduced in the others. A depth map cannot dither, so a tree, fern or prop that switched mesh at a distance moved its shadow in one frame; at a cascade edge the receivers blend both cascades. Low and medium keep the distance switch, which is cheaper with their larger first cascade.
- Dinosaurs leave a level only 8% past its threshold (hysteresis), so an animal at the boundary does not crossfade back and forth.

Popping is measured with `capture --path lod_forest,lod_meadow --fps 60` (`CRET_PATH_STILLS=1` saves every frame) and `tools/lodpop.py`, which counts screen tiles whose change spikes in one frame over both neighbours, leaving out the sky (the clouds change every frame by design). The counts before and after are in docs/quality.

## Night

Night is shot day-for-night. Once the sun is well below the horizon, the moon (opposite the sun) becomes the key light, so shadows, clouds and water keep working.
The frame is lit like day and then graded in `post.fs`:
- **Cool key light.** At night the transmittance LUT carries `MOON_TINT` (`common.glsl`), so the moonlight, the moon disk and the moonlit sky are blue-white.
- **More sky light than sky.** The sky-view LUT is scaled by `NIGHT_SKY_LIGHT`, so shadows, forest and the sides of animals stay readable. The dome you see keeps only `NIGHT_DOME` of that gain (`sky.fs`, and the water's sky reflection), so it stays dark and starry. Moonlit clouds are dimmed to silver-grey.
- **A light floor.** A faint starlight and airglow term in the sky-view LUT keeps ambient light during the minute after sunset before the moon clears the horizon, so dusk and dawn never go black.
- **Grade.** Exposure drops 1.5 stops at night. Rod vision (Purkinje) then shifts dark tones toward a desaturated blue, and moonlit highlights keep a little colour. The tone curve's toe softens (slope 1 at black instead of ACES's crush), so the low stops where a moonlit scene lives stay apart.

Eye adaptation still works at night: it brightens dark views by up to 3 stops over the sun-based exposure, at 60 to 85% strength.

## Interface

The HUD (`hud.c`, `hunt_hud.c`), the touch controls (`touch.c`) and the menus (`menu.c`) share one design system, `ui_style.h`: four text sizes (12, 15, 20 and 40 units, plus the title's name), a 4-unit spacing step, colour roles (ink, dim, faint, amber accent for what to act on, green for your group, warning, danger, and the four vitals' colours), one panel style (a dark, slightly see-through fill with a faint light edge), one icon stroke and the timings (150 ms fades, hints that fade after 5 s, the "F1 help" hint for the first minute). Icons are drawn from lines, arcs and polygons at the HUD's scale (`cret_ui_icon`, `UI_I_*`): no image files.

Shapes are drawn from a white block in the font atlas and lines as quads, so text and shapes share one texture and a screen batches into a few draw calls; the redesigned HUD costs no more CPU than the old one.

Settings > Dinosaur animation switches the procedural layers drawn over the dinosaurs' clips (`render/dino_proc.h`), one switch each: lean into turns, chest follows the slope, spine bends in turns, tail swings and trails, steady head, head looks at animals, breathing, feet planted on the ground, plus one for all of them. They are saved as `animation_layers` (a bit mask) in the settings file and change only what is drawn. `CRET_ANIM` in the environment wins over the saved choice, as `CRET_PRESET` does.

Settings > Plant cards switches the three ways leaf cards are made to look solid ([docs/plants](docs/plants/README.md)): rounded leaves, creased leaflets and blades, and fading leaves seen edge on, plus one for all of them. They are saved as `plant_cards` (a bit mask). The edge fade changes at once; rounding and creases are baked into the plant meshes, so switching one rebuilds the meshes and the tree impostors at the start of the next frame (about a second). `CRET_VEG_ROUND`, `CRET_VEG_CREASE` and `CRET_VEG_EDGE` in the environment win over the saved choice, switch by switch.

**Look studio** (`studio.c`, `render/look.h`; Settings > Look studio, or F3 in play): a panel down the right of the live game with every knob of the picture: exposure and tone curve, colour, effects, atmosphere, light and foliage. It also has looks to load and save, a split compare against the look from before (or Default), a hold-to-see-before button and key (B), a screenshot button and the time, clouds and weather for judging. While it is open the HUD is hidden, the mouse is free and the right button held over the scene turns the camera. The look in play is `look.ini` next to `settings.ini`, saved looks are `looks/NAME.ini`, and screenshots go to `screenshots/`. Every knob's default draws today's image bit for bit. The owner's guide, the shipped looks and their contact sheets are in [docs/look_studio](docs/look_studio/README.md). The panel is desktop only; the web build applies the chosen look.

`capture DIR --ui` (`uicap.c`) draws every screen over the world: title, settings, key bindings, credits, dinosaur animation (`anim`), pause, scenarios, the carnivore HUD at noon and at night, the herbivore HUD, sniffing, death, the island map, the F1 help and the Look studio (`studio`), as `DIR/ui_NAME.png`, and prints the HUD's CPU ms per draw on each play screen. `--size 844 390` with `CRET_TOUCH=1` gives a phone held sideways; `--shot carn,map` picks screens; `CRET_UI_REUSE=1` keeps the rendered backdrops between runs.

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

Hotkeys: F6 spawns a random dummy and F7 a random passive animal 8 m ahead; F8 toggles labels and F9 colliders. In play, F1 opens the key help (with these keys and a status line: fps, time, position); a small "F1 help" hint shows for the first minute. `CRET_CONSOLE="god; spawn trex hostile; labels"` runs commands at start.

### Gallery: `play gallery`

A flat, neutral 1 km plain (baked once into the world cache as seed 0x6A11E7) with the real sky, sun, shadows, LODs and materials. Rows, front to back:

- **Trees:** every tall kind and variant (conifer, broadleaf, cycad, tree fern, palm).
- **Ground cover:** fern, horsetail, shrub, moss carpet, meadow grass and wet grass, each grown from a painted 16 m patch.
- **Plants:** one specimen of every ground species (ferns, horsetails, shrubs and herbs), clubmoss and moss, water lilies, fungi and every fallen-food item, labelled with the species.
- **Props:** every catalog variant at a mid size of its class.
- **Dinosaurs:** one adult of each species playing the chosen clip.
- **Nests:** one row per species with 0 to 5 eggs, then the same nest a third and two thirds built.

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

`capture DIR --gallery [--size W H]` writes each row in pages without a window (`gallery_trees_01.png`, …), and a close-up of every fruiting or flowering species, each lily, fungus and fallen item (`gallery_fruit_<kind><v>.png`, 2 m from its outermost fruit or flower). It also writes the dinosaurs running with skeletons, attacking in wireframe, the trees as impostors, and an overview. `CRET_GALLERY_LODS=1` adds every tree page forced to each LOD from the same camera (`_full`, `_reduced`, `_impostor`): the three should differ only in detail, never in where a branch is.

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
| `wounds` | A gallimimus with a bad leg wound and a body wound limps away from its herd, dripping blood; a hungry AI raptor starts 40 m behind (README, Wounds). Actors take `wound = leg 0.8` (leg, body or head) for their first animal. |

`play scenario` with no name, the title screen's "Fights and scenarios" button and Esc during a scenario open the scenario menu. Its left column, Watch, has Resume, Restart, every scenario by title, Gallery, Quit. Its right column, Play a fight, sets up **your fight**: your species and size (hatchling, juvenile, adult) against 1-8 of a species at a size, which fight you, run the game AI, run away or stand still; Start plays it with you in control (`you = on` in a scenario file does the same). The title screen also has a Gallery button. Scripted movers (walk, flee, hit-and-run, hostile actors) steer round trunks and rocks the way bots do (`cret_avoid_heading`), and one wedged in place backs out before it sidesteps. In play: M takes over the player, C cycles the camera (follow, side, orbit, high), F toggles a free camera, Space pauses, [ and ] change the speed, R restarts, and the console works. The summary prints when the scenario ends: damage, deaths, hits and ability uses per animal.

`capture DIR --scenario NAME` writes, without a window, `DIR/NAME.mp4` (30 fps, the scenario's camera, ffmpeg or `$CRET_FFMPEG`), `DIR/NAME/tNNN.png` every 5 s and `DIR/NAME_summary.txt`.

### Headless tools

- `make -C ocean/cretaceous combatprobe && ./build/cretaceous/combatprobe [--trials N] [--only NAME] [-v]` fights and chases the pairings that matter with a scripted player that has human limits, and prints hit rates, why misses missed, dropped clicks, press-to-hit times, time to kill and chase outcomes (docs/testing.md).
- `make -C ocean/cretaceous scnrun && ./build/cretaceous/scnrun [NAME...] [--seed N] [--trace S]` plays scenarios without a GPU and prints their summaries (the trace also lists moved props).
- `make -C ocean/cretaceous animcheck && ./build/cretaceous/animcheck [--species S] [--check] [--limit NAME=V] [FILE.dino...]` checks dinosaur clips without a GPU: foot slide, ground, loop seams, bone drift, joint pops and the slip at each sim gait (docs/testing.md).
- `make -C ocean/cretaceous stuckprobe && ./build/cretaceous/stuckprobe [NAME...]` plays scenarios and, whenever an animal is told to move but stands still for half a second, prints what holds it: a trunk, a prop (kind, variant, height against the ground), another animal, or nothing (then the slope).
- Scenarios can place movable props: `prop = boulder|stone|log X Z [KG] [YAW]` in `[scenario]` (up to 8).
- `make -C ocean/cretaceous fightprobe && ./build/cretaceous/fightprobe [--trials N] [--first SEED] [--hostile] [--only NAME]` stages bot-against-bot fights per pairing (a raptor, three raptors or a tyrannosaur against each herbivore) and prints who won, the spread, the fight length and knockdowns per fight (see Knockdowns). Unlike `combatprobe` nobody plays: it measures balance, not feel.
- `make -C ocean/cretaceous netbench CC=clang && ./build/cretaceous/netbench [--scenario lan,80ms,...] [--clients N] [--snapshot-hz 15|20|30] [--record FILE | --replay FILE [--check] [--window T W]]` runs a server and N clients over localhost behind simulated links and prints bandwidth, latency, prediction, interpolation and hit registration; a recording replays the same fights for another build or setting (docs/net.md, netbench and Replay).
- `make -C ocean/cretaceous terrashade CC=clang && ./build/cretaceous/terrashade --shoulders DIR | --ravines DIR | --island FILE.jpg | --relief | --coverage | --site K --out FILE.jpg | --at X Z --size M --out FILE.jpg` draws the island without a GPU (a software renderer: the drawn ground hillshaded and coloured by its materials, water, tree trunks and the props' real meshes, open backs near black) and writes JPEGs: before/after pictures of rock shoulders (before: the same bake with `shoulders=0`) and ravines (stepped against wide), the whole island with the shoulders (red) and summits over 300 m (white), or one place from above and at an angle. `--relief` prints the island's peaks, summits and hypsometric curve; `--coverage` how much of each rock shoulder's wall its rock covers from the review views. Build it with the compiler the game and tests use (clang): a gcc build bakes a slightly different island into the same cache file.
- `make -C ocean/cretaceous popprobe && ./build/cretaceous/popprobe --minutes 20 [--restore far] [--player explore] [--pop 0]` measures how far the player is from the nearest animal over a session.
- `make -C ocean/cretaceous critprobe && ./build/cretaceous/critprobe [--species S] [--critters 0|1] [--sessions N] [--minutes M]` plays a scripted hatchling: time to its first meal, food by minute, its first quest, what it caught (see Small prey).
- `make -C ocean/cretaceous questprobe && ./build/cretaceous/questprobe [--species S] [--stage 0|1|2] [--sessions N] [--minutes M] [--veteran] [-v]` plays scripted players of every species and growth stage following their quests: quests offered and done per hour, infeasible ones, time travelling, doing and feeding, deaths and starvation, quest distances, first steps (see `docs/progression.md`).

### Population director (`population.c`)

In solo, host and server play, the island holds 48 animals instead of 16. When nothing is within reach, the director moves herds and predators to 130–230 m from each player, and it places a returning character near a herd.

`CRET_POP=0` restores the old population.

Where the director runs, calm bot groups with no person within 1200 m (past every drawn dinosaur) are simulated as one record each (`fargroup.c`, docs/ecosystem.md, Far groups): out of the physics world, moving along their drive at the slowest member's pace, grazing, drinking, aging, healing and starving by the full sim's own equations, and hunted by rule with the odds the full sim measured. A person within 1150 m brings them back in formation on clear, dry ground. Measured at the probes' 400 m: with nobody about, 39% of the animal-time is folded and the bots cost a third; with a person in a corner, 48 bots on the island cost 0.65x per animal-step. `CRET_FAR=0` keeps everyone in the full sim. The trainer never folds (every slot is a learner).

## Debug knobs

| Variable | Effect |
|---|---|
| `CRET_TERRA="k=… uplift=… diffusion=… years=…"` | Overrides bake parameters (also `bake --set`). `peaks=P` raises the high ground (default 0.5, 0 none), `bedrock=K` scales the bedrock shelves and crags (default 1, 0 none; see Bedrock), `shoulders=1` bakes the rock shoulders (off by default); `ravine=0/1/2` picks no, stepped or wide ravines (cut at load, no bake; see Ravines). |
| `CRET_CRITTERS=0` | No small prey (solo, host, server). `CRET_CRITTERS_INI=path` reads another file than `config/critters.ini`. |
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
| `CRET_GALLERY_MATCH=conifer2,...` | `capture --gallery` shoots each listed tree alone from 30, 150 and 300 m at each LOD (`tools/lodmatch.py`). `CRET_GALLERY_MATCH_DIST=3,30` picks other distances (under 10 m the eye is 1.2 m up, aimed 0.8 m up the trunk), `CRET_GALLERY_MATCH_AZ` swings the camera round the tree (degrees; 180 looks at the sunlit side), `CRET_GALLERY_MATCH_AIM=0.6` aims at that share of the tree's height (looking up into the crown from under it) and `CRET_GALLERY_MATCH_EYE` sets the eye height (m), `CRET_GALLERY_MATCH_FRAMES` sets the frames per shot (30). |
| `CRET_BARK_RELIEF=0`, `CRET_BARK_STUBS=0` | Trunks without the bark relief or the stubs (comparison only). |
| `CRET_DUMP_ATLAS`, `CRET_DUMP_IMP` | Writes the foliage atlas or impostor layers as PNGs. |
| `CRET_MOUSE_RAW=0/1` | Forces raw mouse motion off or on (default: on where supported, off under WSL). |
| `CRET_MOUSE_DEBUG=1` | Prints every mouse delta and every dropped one. |
| `CRET_SHADOWS` | Sun shadow quality: 0 off, 1 low (3 cascades at 1024² to 240 m), 2 high (default; 4 at 2048² to 800 m), 3 ultra (4 at 3072² to 800 m). |
| `CRET_SHADOW_RES`, `CRET_SHADOW_CASCADES`, `CRET_SHADOW_FAR` | Override the shadow map size, cascade count and last cascade distance (m). |
| `CRET_SHADOW_DEBUG` | Prints shadow GPU ms per cascade, caster counts and whole-frame GPU ms every 24 frames. Do not combine with `CRET_PROFILE` or `CRET_PROP_DEBUG`, because elapsed-time queries cannot nest. |
| `CRET_SHADOW_VIS=1` | Receivers show the shadow term tinted by cascade (red, green, blue, yellow). |
| `CRET_SHADOW_NUDGE="dx dy dz"` | Moves only the cascade fit. With texel snapping, the image must not change (stability test). |
| `CRET_PRESET=low\|medium\|high\|ultra` | Quality preset (default high): render scale, shadow maps, cloud steps, plant and prop ranges, sward and grass density, terrain parallax and range, anisotropy. Single knobs such as `CRET_SHADOWS`, `CRET_CLOUD_QUALITY` and `CRET_RENDER_SCALE` override it. |
| `CRET_SHARPEN=S` | Strength of the full-resolution sharpen on high and ultra (default: the look's Sharpen, 0.6; 0 off); wins over the look. |
| `CRET_GL_STATS=1` | Native: prints draw calls and triangles per pass and fragments shaded per frame (pipeline statistics query) with the GPU profile, per shot with `CRET_CAPTURE_FRAMES=N` (stalls each frame: counts, not timing). |
| `CRET_GPU_PROFILE=1` | Per-pass GPU and CPU ms (timer queries), printed per shot by `capture` with `CRET_CAPTURE_FRAMES=N`. "present" is the swap and the time until the next frame. Do not combine with the other GPU timers above. |
| `CRET_PROFILE_EVERY=N` | With `CRET_GPU_PROFILE=1`: prints the per-pass report every N frames of the title and of play, including the sim ticks, bots, HUD and the wait for the next frame (on web the browser's console; `tools/web_smoke.js --perf` reads it). |
| `CRET_BENCH_CSV=PATH` | With `CRET_GPU_PROFILE=1`: every per-pass report is also appended to PATH as CSV rows `run,shot,frames,pass,gpu_ms,cpu_ms` (`run` from `CRET_BENCH_TAG`). The Windows package's `Benchmark.bat` uses it: nine fixed views at 1920x1080 on every preset plus high with ambient occlusion off, 120 frames each; `python3 tools/gpubench_table.py gpu_bench.csv` turns the file into a frame-time table per shot and a per-pass table per preset. |
| `CRET_AO=S` | Ambient occlusion strength (default 1 on high and ultra, 0.6 on medium, 0 on low). The pass (`shaders/ao.fs`) samples twelve directions per pixel in a 4x4 pattern of rotations and `ao_blur.fs` averages each 4x4 block, skipping texels at another depth; until 2026-11-14 it used a random rotation per pixel inside `post.fs`, which left a stipple round every plant and fine vertical stripes on bare ground at full resolution. |
| `CRET_TEX_COMPRESS=0` | Keeps terrain and prop textures uncompressed (default BC3/DXT5). |
| `CRET_NO_CLIP_CONTROL=1` | Tests the reverse-Z fallback for GL without `ARB_clip_control`. |
| `CRET_VEG_DEBUG=1` | Also prints plant instances and triangles per frame, and the tall-grass scatter (clumps, ms). |
| `CRET_GRASS=0` | No tall grass, and no bending from wind gusts or animals. |
| `CRET_GRASS_RANGE=M` | Tall-grass draw range in metres on the high preset (default 45). |
| `CRET_TRAMPLE=0` | Keeps the grass and wind but animals no longer bend it (A/B of the trample map). |
| `CRET_TRAMPLE_DUMP=file.ppm` | Every 30th frame writes the trample map (red, green: bend; blue: trail). |
| `CRET_WEATHER=0\|1\|2` | Forces clear, overcast or rain. By default the weather follows the cloud cover: above about 0.7 the sky greys over, above about 0.9 it rains (streaks, haze, wet surfaces). |
| `CRET_CLOUD_CB=0` | Marches every cloud pixel every frame instead of one per 4x4 block plus history (a reference for the temporal path; about 16x the march cost). |
| `CRET_CAPTURE_WARM=N` | `capture` stills: at least N warm-up frames before the shot (default 64, since 2026-11-14: the cloud history converges over a few 16-frame cycles, and at 24 stills showed grainy cloud edges the game does not; a staged scene still runs only the last 24, so its animals stand where the shot framed them). Global time runs on across a capture's shots, so a shot's clouds and their shadows depend on the shots before it. |
| `CRET_SHADOW_STAGGER=0` | Renders every shadow cascade each frame (default: the two farthest alternate). |
| `CRET_FLUSH=N` | Bitmask of the mid-frame `glFlush` calls (1 after shadows, 2 after terrain; default 3). |
| `CRET_DINO_TEX=N` | Caps dinosaur texture size (for example 1024). |
| `CRET_VEG_ATLAS=N` | Uploads the plant atlas from its NxN mip down (64-512, default 512; the web page's phone profile uses 256). |
| `CRET_VEG_IMP=N` | Tree impostor size (64-256, default 256; the phone profile uses 128). |
| `CRET_VEG_EDGE=0` | Leaf cards no longer fade out edge on, and their backlit glow follows the bent normal again (docs/plants). |
| `CRET_VEG_ROUND=0` | Leaf cards and blades are built without rounded normals across their width (docs/plants). |
| `CRET_VEG_CREASE=0` | Frond leaflets and grass and sward blades are built flat, without their centre crease (docs/plants). |
| `CRET_TOUCH=1\|0` | Touch controls always (screenshots and layout checks on a desktop) or never. Default: shown after the first touch, hidden by a key or a click. |
| `CRET_NO_PROP_COLLIDERS=1` | Props and tree trunks stop blocking movement. |
| `CRET_NO_TREE_COLLIDERS=1` | Tree trunks stop blocking movement (props still do). |
| `CRET_NO_DINOS=1` | Draws agents as capsules instead of dinosaur models. |
| `CRET_NET_LOAD_LIMIT=S` | Server: seconds it waits for a joining client that is loading the world before dropping it (default 300). |
| `CRET_TEST_SLOW_LOAD=S` | Test only (`tests/test_net_join.c`): every world load waits S seconds first, reporting progress like a bake. Never set it in play. |
| `CRET_SHOT_CAM="name:x:z:above:yaw:pitch:fov;..."` | `capture` stills: adds fixed viewpoints (camera `above` metres over the ground or water, yaw and pitch in radians, vertical field of view in degrees), for before and after comparisons from the same place. Names starting `ground_` draw no plants. |
| `CRET_CAPTURE_DEPTH=1` | `capture` stills: also writes `NAME_depth.pgm`, the view depth in decimetres (16-bit, sky 65535), to cut a still into distance bands. |
| `CRET_SHOT_VARIANTS="noon:0.5:0.3,gold:0.70:0.35"` | `capture` stills: renders every selected shot once per `tag:time_of_day:cloud_cover`, as `NAME_TAG.png`. |
| `CRET_LOOK=FILE` | Draws with that look file instead of the player's `look.ini` (captures start from Default). |
| `CRET_SHOT_LOOKS="Default,Richer,FILE"` | `capture` stills: renders every selected shot once per look (a shipped look's name or a look file), as `NAME_LOOK.png` (the Look studio's contact sheets). |
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
