# Web build on phones and tablets

The web build (README, "Web build") also runs on phones and tablets: touch
controls drawn by the game, a page that behaves like an app, and a
small-memory profile for mobile GPUs. This page covers what it does, what it
measures, and what full world streaming would take.

![The title on a phone held sideways](phone_title.jpg)
![Play: the stick pushed to sprint while a second finger turns the camera](phone_move.jpg)

The screenshots come from `tools/web_smoke.js --phone` (headless Chromium,
software WebGL, placeholder capsules for the dinosaurs).

## Touch controls (`touch.h`, `touch.c`)

They are drawn with raylib over the HUD at the HUD's scale. Their sizes are
in CSS pixels, so a button is the same physical size on any screen. On a
phone a button is about 9 mm across and the attack about 12 mm; Apple and
Google ask for at least 7 mm. The player's Interface size setting scales
them too. They show once the screen is touched, or at once on a touch-first
page (a coarse pointer and nothing that hovers). They hide when a key is
pressed, the mouse is clicked or the wheel turns, so a laptop with a touch
screen switches back and forth. Mouse motion alone never hides them: raylib
moves the mouse with a single finger, and a resize moves it too.
`CRET_TOUCH=1` shows them always, natively too, for screenshots;
`CRET_TOUCH=0` never.

| Control | Where | What it does |
|---|---|---|
| Stick | Left 45% of the screen, below the top 30%, wherever the thumb lands | Moves relative to the camera. It is analog: a gentle push walks, so you can creep up on prey. Pushed to the rim it sprints, and the ring turns gold with a "Sprint" label |
| Camera | A drag anywhere else | Turns the camera, 0.006 rad per CSS pixel times the sensitivity setting. When no finger is turning it, the camera swings in behind the running animal, as in the mouse drag scheme |
| Bite / Ram / Tail | Bottom-right corner | Primary attack, with its cooldown sweep |
| Slash / Swipe | Up and left of it | Secondary attack (species that have one) |
| Jump | Left of it | Jump (species that can), or "Let go" while latched |
| Eat / Drink | Above it | Hold to eat. It reads "Drink" when water is ahead, as E does |
| Carry / Drop | Left of the right-hand arc, carnivores only, shown when something lies at the mouth or is held | Picks up or puts down food (B) |
| Nest | Above Carry, shown to a fed adult with no nest | Hold to build a nest (N) |
| Call, 1, 2, 3 | Bottom centre, where the ability bar is with a keyboard | Call, and abilities 1-3 with the ability bar's look: initials, a dark cooldown sweep and seconds, a gold ring when ready, blue when short of stamina, a dim "-" while locked |
| Menu | Top right, left of the minimap | The pause menu (Esc) |
| Minimap | Tap it | The island map; any tap closes it |
| Dead | Tap anywhere | Respawn now (Enter) |

Multi-touch: each finger gets one job when it comes down, so the stick and
the camera work together. raylib 5.5's web touch callback drops the last
entry of its list on every touchend. Its list is ordered by touch id, so
when one of two fingers lifts it can keep the lifted finger and lose the one
still down: lifting the camera finger stopped the stick. `touch.c`
therefore checks raylib's list against the page's own record of the fingers
down (`TouchEvent.touches`), and keeps a finger raylib lost at its last
position. A tap shorter than a frame starts and ends between two polls and
never reaches raylib's list. The page queues every touchstart, so such a tap
still presses a button or picks a menu row. That matters at 10 fps on a
weak phone and in the headless test.

HUD under touch (`CretHud.touch`):
- The vitals become a compact strip under the compass: name, stage and four bars in two columns. The stick takes the bottom left.
- The minimap shrinks from 220 to 180 units. The group panel is hidden; the minimap still shows the group.
- The ability bar's slots and attack radials give way to the buttons; status chips move above the food prompt.
- Prompts name buttons instead of keys ("Eat  graze", "push the stick gently"), and the death screen says "tap to respawn now".
- The keyboard help line is hidden.

Menus by tap:
- A tap is a click that also selects its row. This works on the title, settings, scenario and pause menus.
- Every value that steps shows its `<` and `>` at all times, with a highlight under the mouse: the title's character and species, the pause menu's species and ability 3, every settings row with a value, and the fight rows of the scenario menu. A click or tap on the left half of the value (from `<` to its middle) steps back, on the right half forward; on rows with a bar, only the arrows step and the bar takes the click itself. Only a click on the label picks the row (Play, the character, buying ability 3); on settings rows it just selects it.
- A new character cannot be named without a keyboard, so it gets the species' name and a number ("Velociraptor2").
- The title and settings pages fit a phone held sideways (390 CSS pixels tall). The title closes up its rows; settings moves sound, key bindings and Back into a second column.

## Page (`web/index.html`, `web/loader.js`)

- Viewport: `width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover`.
- Touch handling: no scrolling, pull-to-refresh, pinch or double-tap zoom, text selection, long-press menu or tap highlight (`touch-action: none`, `overscroll-behavior: none`, `user-select: none`).
- The canvas is sized in CSS pixels, not device pixels: a phone renders a third as many pixels in each direction as its screen has at DPR 3, and the HUD's height-based scale then gives legible text.
- Rotation resizes it: raylib's resize callback sets the canvas to the new window size, and the game resizes its targets (`IsWindowResized`).
- The first tap on a touch device asks for fullscreen and, on Android, a landscape lock. iPhones refuse element fullscreen, and their page stays as it is.
- Held upright, a touch device shows a hint to turn sideways; a tap puts it away.
- The message for a browser without WebGL2 is unchanged.

## Keyboard and mouse (`web/loader.js`)

**Short presses.** raylib's web backend keeps only the current state of each key and mouse button. GLFW's listeners set it as the browser's events arrive. The game asks whether a key went down since the last frame (`IsKeyPressed`, `IsMouseButtonPressed`). A click or a tap of a key that went down and came up again while a frame was being drawn was therefore never seen. A quick click or tap lasts 50 to 100 ms. At 15 fps, menu clicks and Enter on the title did nothing, and at 10 fps most attacks, jumps, B and 1-3 were lost.

The page now holds key and mouse button events back:
- An event goes on to GLFW only once the game has begun a frame since that key or button last changed.
- Until then it waits, in the order it came, and goes on when the next frame begins. The frame count is `Module.cretFrames`, which `cret_web_next_frame` in `cretaceous.c` raises once per frame.
- So every press is down for exactly one frame, however short it was and however slow the frame. A key pressed twice within a frame counts twice, one frame apart.
- A key held longer than a frame changes the moment it does, as before. The one-frame minimum is the only difference.
- While a press waits, the events after it wait too, so presses stay in order. Down, Down, Enter within one frame moves the menu two rows and then picks.
- Key repeats change nothing and pass straight on. A repeat of a key whose press is still waiting is dropped, so it cannot become a second press.
- Characters (typing a name, the console) come through `keypress` and are not held back.

Because raylib's own state is corrected, every reader sees the press: the title, settings, scenario and pause menus (`menu.c`, `hud.c`), play input (`player_input`, which already carries presses to the next 30 Hz tick), the map, the debug console and free cam. None of them changed. The native build has no such page and is unchanged.

**Focus.** The first click or touch on the canvas gives it keyboard focus. Keys reach the game wherever focus is on the page; inside an iframe (a claude.ai Artifact), the click is what brings the keys to the page at all.

**Leaving the page.** On alt-tab or a tab switch, every key and mouse button still down is let go, and presses still waiting are dropped. GLFW used to let go of the keys itself, but never of the mouse buttons, so a button held while switching away stayed down. A button let go off the canvas is released too, for example after dragging out of a window that is not fullscreen. The page now does all of these releases through the same queue, so a press passed on just before still gets its frame.

**Esc in pointer lock.** Play locks the pointer (mouse look). Desktop browsers keep the Esc that ends a pointer lock for themselves and do not pass it to the page, so it took a second Esc to open the menu. Now, when the lock ends without the game asking, the page passes on one Esc press, and the menu opens as the README says. This covers Esc and also switching away. When the game lets go of the lock itself (the menus, Tab), nothing is sent. Headless Chromium does deliver that Esc, and so may some browsers. An Esc the browser delivers within half a second of the lock ending is not counted again. After Resume, the next click takes the lock again. Browsers refuse a new lock for about a second after the user ended one.

Measured with `tools/web_smoke.js --short-press` (headless Chromium, SwiftShader, every press 10-30 ms long):

| | Presses lost (title / play) |
|---|---|
| 5.0 | MEASURE |
| Now | 0 of 8 / 0 of 12 |

## Settings and characters across visits

The game writes `settings.ini` and `characters/<name>.ini` under
`/home/web_user/.config/cretaceous` in the in-memory file system, as it does
natively under `~/.config/cretaceous`. That file system is gone on reload, so:
- After every write the game calls `cret_config_saved` (`character.c`), and the page copies the directory's `.ini` files into one `localStorage` key, `cretaceous-config-v1` (file name to text).
- Before `main` runs, the page writes them back (`restoreConfig` in `web/loader.js`). That is a synchronous read of a few kilobytes: the title is not delayed.
- On web a change in Settings is saved at once, not only on Back, since a reload never says Back.
- The character is saved when the page is hidden or left (`visibilitychange`, `pagehide`): the page calls the exported `cret_web_save`, which runs while the game's loop waits for its next frame, so the sim is whole. Natively it is saved each minute, on death and on quitting, as before.

Why `localStorage` and not an IndexedDB mount (Emscripten's IDBFS): it is
synchronous, so a save is in storage before the page can go away
(`FS.syncfs` is asynchronous and may not finish in `pagehide`); there is
nothing to wait for before the title; and it needs no extra runtime library.
The files are small (about 300 bytes of settings and 400 per character, with
at most 32 characters), far under the 5 MB a site gets.

Private windows keep the key for the session. Where storage is blocked
(`localStorage` throws) or full, the page goes on without it and the game
forgets on reload, as before. The derived-data cache stays in Cache Storage
(above).

## First-visit quality

Used only while no settings are saved. The page decides what kind of device
it is on and tells the game (`CRET_WEB_QUALITY`); `cret_settings_default`
(`menu.c`) picks the start:
- **Low profile:** a touch-first page, a GPU without S3TC (the small-memory profile above) or `?mobile`; or a software renderer (`WEBGL_debug_renderer_info` names SwiftShader, llvmpipe, softpipe, "Software" or Microsoft's Basic Render driver). Low preset, render scale 0.7, no automatic stepping. The renderer's name is only a hint, never a GPU database.
- **Everything else** (a desktop with a hardware GPU): the high preset at render scale 1.0, with automatic stepping on.

Automatic stepping (`CretQualityWatch`, `menu.c`):
- **When.** It watches the first seconds of the title, and again of play.
- **Measuring.** Each watch skips 2 s and at least 3 frames (loading hitches, shader compiles), then measures at least 4 s and 4 frames.
- **Stepping down.** If most measured frames (the median, so one long hitch does not count) were slower than 30 fps, it lowers the quality one step and measures again.
- **The steps.** Render scale 1.0 to 0.85 to 0.7; then the preset high to medium to low, at 0.7; then low at 0.5.
- **Ending.** A watch ends when a measurement passes or nothing is lower. It never steps up, so it cannot go up and down.
- **The note.** The first step of a visit says so once: under the title menu, or as a HUD notice in play ("Graphics lowered to High at 85% to keep the game smooth. Change it in Settings.").
- **Keeping it.** Each step is saved with `auto_quality = 1`, so a later visit starts there and keeps watching.
- **The player wins.** Changing Graphics quality or Render resolution in Settings turns the stepping off for good (`auto_quality = 0`).
- **Fixed by the page.** `?preset=` or `?env=CRET_RENDER_SCALE=` turn it off for that visit.

On the GTX 1060 (60 fps on low, 50 on high at full resolution) a first visit
stays on high at full resolution.

## Keys the browser takes

The browser handles some keys before the page sees them, and a page can
cancel others only with `preventDefault` on the key event (`browserKey` in
`web/loader.js`):

| Key | The browser's action | Web build |
|---|---|---|
| Ctrl+W (walk forward natively) | Closes the tab; no page can stop it | Walk is Left Alt on web (`CRET_WALK_KEY_NAME`, `CRET_WALK_DOWN` in `hud.h`; the help line, the key bindings page and the prey prompt name it). Ctrl still walks. Leaving a game in progress asks first (`beforeunload`), and in fullscreen the keyboard lock hands Ctrl+W to the game |
| Ctrl+T, Ctrl+N, Ctrl+Tab | New tab, new window, next tab; cannot be stopped | Only reachable while walking with Ctrl; fullscreen locks them too |
| Ctrl+S, D, R, F, P and other Ctrl combinations | Save page, bookmark, reload, find, print | Kept from the browser while playing (on the title they work as usual) |
| F5 (reload shaders) | Reload | Kept while playing; reload on the title |
| F1 (help line) | Browser help | Kept |
| F2, F3, F4, F6 to F10 | Find (F3), address bar (F6), caret browsing (F7), menu bar (F10) | Kept (the game uses F2 for wireframe, which WebGL ignores, F6 to F9 in the debug console and F10 in the gallery) |
| Alt alone (walk) | Focuses the menu bar on Windows, after which keys go to the browser | Kept, down and up |
| Alt with a key (Alt+W, Alt+D, Alt+F, Alt+Left) | Address bar, browser menu, back | Kept |
| Left Alt + Shift | Switches the keyboard layout on Windows if several are installed | The operating system's; walk and sprint together are not needed |
| Tab, Backspace | Focus change, back | Kept (GLFW and the page's press queue) |
| Esc | Leaves pointer lock (and fullscreen) | Opens the menu as natively (Keyboard and mouse, above); in fullscreen the keyboard lock gives Esc to the game and the browser asks to hold Esc to leave |
| ` (console) | Nothing | As natively; F12 opens the browser's developer tools instead of the console |
| F11, F12 | Browser fullscreen, developer tools | The browser's |

**Fullscreen.** Settings > Display enters the page's own fullscreen
(`requestFullscreen`), as does the first tap on a touch device. While in it the
page takes the Keyboard Lock API (`navigator.keyboard.lock()`, Chrome and
Edge), so every key, Ctrl+W included, reaches the game. The browser's own F11
fullscreen does not allow the lock. Settings shows the page's real state, so
leaving fullscreen by holding Esc shows Window again.

**Leaving.** While play runs (`Module.cretPlaying`, set by
`cret_web_playing` in `cretaceous.c`), `beforeunload` asks before the page is
reloaded, closed or navigated away (Ctrl+W, Ctrl+R, the back gesture). The
browser shows its own wording. "Play a fight" from the scenario menu does not
ask.

The native build keeps Ctrl for walking and has no such page.

## Small-memory profile

Phones have no S3TC (BC3), so every game texture stays RGBA8 there: four times the memory of the desktop's BC3.
- **Who gets it.** The loader turns the profile on when the page is touch-first or the GPU lacks `WEBGL_compressed_texture_s3tc` (and its sRGB variant), or with `?mobile`. A desktop is unchanged.
- **What it sets.** Ground materials, props and dinosaurs at 512 texels (`CRET_MAT_SIZE`, `CRET_PROP_TEX`, `CRET_DINO_TEX`), the plant atlas at 256 (`CRET_VEG_ATLAS`, new) and tree impostors at 128 (`CRET_VEG_IMP`, new).
- **Why that loses nothing.** A phone held sideways shows the 3D view at about 590 x 273 pixels at the low preset's render scale of 0.7, so none of this is visible.
- **Atlas.** The atlas is still painted at 512 and uploaded from its 256 mip down. Its cache key changes only when it is capped.
- **Preset.** These pages start on the low preset (render scale 0.7, three 1024² shadow cascades to 240 m, plant ranges at 0.6, props at 0.5, a third of the cloud steps); see First-visit quality below.
- **Checked natively** (`CRET_PRESET=low` against high, `CRET_VEG_DEBUG=1`, 844x390): shadows drop from 4 cascades at 2048² to 800 m to 3 at 1024² to 240 m. Trees drop from 1.10M to 0.52M triangles, ground plants from 511k to 151k, and sward tufts from 16k to 10k.
- **Knob order.** The pack's knobs come first, then the profile, then `?env=`, which now wins over both (before, the pack's `CRET_MAT_SIZE` overrode `?env=`).

ETC2 (`WEBGL_compressed_texture_etc`, common on mobile WebGL2) is not used.
`render/texbuild.c` would need an ETC2 RGBA encoder (EAC alpha plus the
ETC1/T/H/planar colour modes) with a decoder for its tests. That is a few
hundred lines, and on a phone it would run single-threaded in wasm on every
first visit. With the profile's sizes, the textures ETC2 could compress are
small: ground materials 11 MB, plant atlas 11 MB, impostors 25 MB. What is
left of GPU memory is mostly the 2048² terrain textures, which are floats or
need exact values and would not be block-compressed anyway. So it is RGBA8
at reduced sizes.

## Memory

Measured by `tools/web_smoke.js` in headless Chromium on SwiftShader. The
wasm heap is the size of its buffer: it only grows, in steps, so the
allocator's figures are given too (in use now, and the arena, its high-water
mark). Files are the in-memory file system. GPU is an estimate: every texture
level and renderbuffer the page allocates, counted by wrapping WebGL.
Processes are the resident sizes of Chromium's renderer and GPU processes.
SwiftShader keeps the GPU's memory in the GPU process.

| Phone (390x844 at DPR 3, held sideways, no S3TC) | Wasm heap | malloc in use / peak | Files | JS heap | GPU estimate | Renderer | GPU process |
|---|---|---|---|---|---|---|---|
| 5.0, title | 531 MB | not reported | 485 MB | 497 MB | 403 MB | 1188 MB | 742 MB |
| Now, title | 443 MB | 188 / 352 MB | 72 MB | 78 MB | 231 MB | 696 MB | 511 MB |
| Now, play | 443 MB | 194 / 352 MB | 72 MB | 78 MB | 235 MB | 699 MB | 523 MB |

The 5.0 row is the 5.0 build under the same emulation. It cannot get past
the title there: it has no touch input.

| Desktop (960x540, S3TC) | Wasm heap | malloc in use / peak | Files | JS heap | GPU estimate |
|---|---|---|---|---|---|
| Now, title | 443 MB | 187 / 396 MB | 105 MB | 117 MB | 342 MB |
| Now, play | 443 MB | 192 / 396 MB | 39 MB | 117 MB | 346 MB |

What changed:
- **The island's file.** It is deleted from the in-memory file system once loaded (`world.c`): 185 MB. The loader also stops holding the inflated bundles, and gives the smaller files of a bundle their own copies, so the island's buffer can actually go. The world then stays loaded for the whole visit, so a scenario after the title does not need the file again.
- **Bake-only fields.** Rain, soil and temperature are skipped when the browser loads the island (`terra.c`; `web_pack.py` already shipped them zeroed): 48 MB of the wasm heap.
- **Terrain cache items.** On web they are no longer written to the in-memory file system (`render/cache.c`). The page never kept them, and they held about 130 MB until it deleted them.
- **The profile above.** Impostors went from 100 MB to 25 MB and the plant atlas from 44 MB to 11 MB.

What is left (phone, title):
- **wasm heap.** malloc has 188 MB in use, 172 MB of it the world. That is the runtime fields at 32 bytes a sample (134 MB), the B-spline height coefficients (17 MB) and Box3D's heightfield (21 MB). The heap stays at 443 MB because loading peaks at a 352 MB arena (terrain derivation temporaries next to the world), and a wasm heap never shrinks.
- **GPU.** The nine 2048² terrain textures (about 150 MB), impostors (25 MB), the three 1024² shadow cascades (12 MB) and the render targets.
- **Files.** The texture cache items until the page has saved them to browser storage, then the assets and the cloud volumes (about 40 MB).

That totals about 0.7 GB of page memory plus about 0.25 GB of GPU memory.
It is within what recent phones give a tab (roughly 1 to 1.5 GB on iPhones
and Android phones with 4 GB or more). On a 3 GB phone it is tight. The
next cheap win there is the load peak: building the terrain textures in
bands would cap the arena near the steady 190 MB, and the heap near 250 MB.

## World streaming: what it would take

Not built. The 2048² island (4 m cells, 8.2 km across) is held whole, 172 MB
at runtime on web. The derived terrain textures add about 150 MB of GPU
memory.

Runtime fields per sample: height, water and flow (floats), water kind,
flow direction (2 bytes), splat weights (8), vegetation weights (8) and
hardness. That is 32 bytes, plus 4 for the B-spline coefficients and about 5
for Box3D's heightfield.

What reads the world directly:
- Most code goes through `cret_height` and `cret_water` (B-spline coefficients and the water field).
- Some code indexes `CretWorld.map` arrays itself:
  - `sim.c` and `critters.c`: food from vegetation, water kind.
  - `progress.c`: quest places, a scan of the whole map once per world.
  - `scenario.c`.
  - `audio.c`: ambience from water kind, splat and vegetation near the listener.
  - `hud.c`: the minimap, built from the whole map on first draw.
  - `cretaceous.c` and `capture.c`: review shots and the title flyover scan the map.
  - `render/view.c`: the nine whole-island terrain textures and the horizon bake.
  - `render/veg.c` and `render/veg_place.h`: scatter rings round the camera.
  - `render/props_place.c`: prop tiles.
  - `render/dino.c`: feet on the ground.
  - `world.c`: the Box3D heightfield over the whole island.

A plan, in order:
1. **Chunks.** `web_pack.py` writes the runtime fields in 256² chunks (1 km, about 9 MB each before compression), plus a quarter-resolution overview (512²: height, water, splats; about 3 MB). The loader keeps the chunks gzipped in memory (29 MB for the island) or fetches them on demand, and inflates them when the game asks.
2. **Accessors.** Every direct `map.field[i]` read goes through accessors that find a sample's chunk in a 8 x 8 table and load it if missing. The sim must read full-resolution data wherever an agent is, so the resident set is the union of a window round every agent (5 x 5 chunks round the player is 25 chunks, about 60 MB with coefficients and collision). The population director already spawns and retires animals round the player. The RL environment, the server and native play keep everything resident: streaming is a web-only policy behind the accessors, and results stay identical.
3. **Collision.** One Box3D heightfield per chunk, created and destroyed with residency, as `CretPropStream` already does for prop tiles.
4. **Terrain rendering.** The nine whole-island textures become a clipmap: a 1024² window at full resolution round the camera, updated in strips with `texSubImage` as it moves, over the overview for the distance. The horizon bake runs per chunk in the packer, and its results ship.
5. **Whole-map scans** (quest places, minimap, flyover shots) move to the packer or read the overview.

Items 1-3 take the world from 172 MB to about 65 MB. Item 4 takes the
terrain textures from 150 MB of GPU memory to about 40 MB, and it is the
largest piece of work.

## Frame time

On the owner's PC (GTX 1060 through ANGLE/D3D11, 1512x795 canvas, low
preset) play as a raptor hatchling in dense ferns ran at about 15 fps while
another job used most of the CPU; native was smooth. What made the web build
slower, and what changed:

- **The CPU waited for the GPU every frame.** Each frame ended by waiting for
  a fence on the frame just submitted (`cretaceous.c`, `cret_web_next_frame`),
  so the CPU never built frame N+1 while the GPU drew frame N: a 30 ms CPU
  frame and a 20 ms GPU frame took 50 ms, rounded up to 66 ms (15 fps) by the
  60 Hz display. It now waits for the previous frame's fence: CPU and GPU
  overlap as natively, and at most one finished frame queues for the GPU, so
  the memory blow-up the fence was added against cannot come back.
- **Too many WebGL calls.** Each call is a call from wasm into JavaScript,
  validated by the browser and encoded as a command for its GPU process,
  which validates it again and translates it to D3D11 (ANGLE). That GPU
  process competes for the same busy CPU. Calls per frame (fence polls not
  counted) fell to about half, draws by 30 to 50%, buffer uploads from about
  300 calls to about 20:
  - uniform locations are cached by program and name (`render/gfx.c`, `gfx_loc`);
  - instance attributes are enabled once per VAO, and each draw only points
    them at its list (`render/veg.c`, `render/props.c`);
  - instance lists go up in one `glBufferData` per pass instead of a
    `glBufferSubData` per list (`gfx_stream_*`);
  - terrain and water draw their quadtree patches and the open-ocean ring
    as instances of two patch meshes (`render/view.c`, `node_instances`;
    `terrain.vs`, `water.vs`), not a draw and three uniforms per node;
  - all dinosaur poses go up in one texture upload (`render/dino.c`).
- **Far trees were culled and re-uploaded every frame.** Every tree out to
  4.5 km went through the CPU cull and into the instance buffer as an
  impostor each frame: 3.7 MB per frame on the title, 3 to 7 MB in play.
  They now sit in one static buffer (12 MB of GPU memory, made at the first
  frame) in 512 m blocks; `veg_imp.vs` computes each tree's fade from its
  distance, and a frame draws the runs of blocks in view (`render/veg.c`,
  `build_impostors`, `select_impostors`). The cull now only visits tiles
  within the mesh range.
- **Plant shadow casters were sorted with `qsort`** (a comparator call per
  comparison, which wasm cannot inline) for front-to-back order. A 256-bucket
  counting sort does it in linear time; a depth map does not depend on draw
  order, so shadows are identical.
- **The wasm build** is now `-O3 -flto` (the sim's tick for 256 animals:
  1.42 ms at `-O2`, 0.7 to 0.85 ms with LTO, `tests/bench.c` under node;
  `-O3` alone changed nothing) with `-sASYNCIFY_IGNORE_INDIRECT` (only the
  direct call path to the frame's yield is rewritten for ASYNCIFY; before,
  every function that calls through a pointer was too).

Nothing here changes what is drawn: native captures (`capture --shot
ridge,coast,sunset,aerial`) are identical pixel for pixel before and after
every step except the static impostors. On Mesa's llvmpipe the 5.0 build
draws no plants at all once the per-frame instance buffer holds the far
impostors (a few MB; the web build on SwiftShader draws them), so there the
static impostors appear for the first time; on the web build the forest
looks the same.

Checked and not the cause:
- **OpenMP.** The only parallel loop in a frame is the tall-grass refresh,
  which runs when the camera crosses an 8 m cell (0.8 to 1.4 ms on 4 threads
  natively, `CRET_VEG_DEBUG=1`); every other per-frame loop is
  single-threaded natively too.
- **Sim ticks.** At most 3 per frame (the frame's dt is capped at 0.1 s, so a
  very slow frame slows the game rather than spiralling); a tick with its
  bots costs 0.2 to 0.8 ms of wasm here.
- **Debug flags.** The release link has no assertions; Box3D builds with
  `-O3 -DNDEBUG` and without its validation; the game has no `assert`.

### Measured

`node ocean/cretaceous/tools/web_smoke.js --perf` in headless Chromium on
SwiftShader (4 cores; the GPU process pinned to two of them and the page to
the other two), 1280x720, low preset, raptor hatchling, 8-frame windows
after 8 frames to settle. CPU is the game's own thread (wasm and the WebGL
calls it makes) per frame, without the wait for the next frame; from
`CRET_GPU_PROFILE=1 CRET_PROFILE_EVERY=8`. SwiftShader draws a frame in 10 to
28 s, so its frame rate says nothing; the CPU side and the call counts are
what carry over to a real GPU. Single runs; passes under 0.5 ms vary by
about 50% from run to run.

| Desktop, 1280x720 | Title before | Title after | Ferns before | Ferns after | Forest before | Forest after |
|---|---|---|---|---|---|---|
| CPU per frame, ms | 8.9 | 5.6 | 16.8 | 5.5 | 10.9 | 7.2 |
| of which plants (cull, refresh, upload, draws) | 3.7 | 0.9 | 7.5 | 1.1 | 3.7 | 1.2 |
| shadow casters | 1.4 | 1.2 | 3.4 | 1.6 | 3.1 | 2.2 |
| terrain and water | 1.1 | 0.3 | 1.1 | 0.2 | 1.0 | 0.3 |
| props | 0.5 | 0.4 | 0.8 | 0.4 | 0.7 | 0.5 |
| sim and bots (3 ticks) | 1.1 | 1.8 | 2.3 | 0.7 | 0.9 | 1.6 |
| HUD | 0.2 | 0.2 | 0.6 | 0.5 | 0.6 | 0.5 |
| WebGL calls (time inside them) | 4288 (3.2 ms) | 2142 (1.4 ms) | 7597 (6.5 ms) | 3764 (2.5 ms) | 7020 (4.0 ms) | 3466 (2.0 ms) |
| draws | 413 | 212 | 705 | 493 | 654 | 447 |
| uploads, MB (calls) | 3.66 (110) | 0.12 (13) | 7.35 (295) | 0.42 (21) | 3.02 (267) | 0.54 (20) |

| Phone emulation (844x390 CSS px at DPR 3, no S3TC) | Title before | Title after | Ferns before | Ferns after | Forest before | Forest after |
|---|---|---|---|---|---|---|
| CPU per frame, ms | 9.4 | 4.7 | 12.3 | 8.3 | 10.8 | 6.1 |
| of which plants | 3.6 | 0.9 | 2.5 | 1.6 | 3.7 | 1.2 |
| shadow casters | 1.2 | 1.0 | 3.6 | 2.1 | 3.1 | 2.0 |
| WebGL calls (time inside them) | 4355 (2.9 ms) | 2151 (1.2 ms) | 7991 (4.0 ms) | 3704 (2.7 ms) | 7192 (4.1 ms) | 3504 (2.0 ms) |
| draws | 420 | 213 | 725 | 476 | 682 | 444 |
| uploads, MB (calls) | 4.02 (113) | 0.14 (13) | 0.81 (318) | 0.38 (22) | 3.39 (263) | 0.59 (20) |

Frame rate in headless Chromium (SwiftShader): 0.10 fps on the title and
0.04 to 0.06 fps in play at 1280x720, both before and after (9.7 and 9.6 s
per title frame; 28 and 18 s in the ferns, 23 and 21 s in the forest);
0.07 to 0.16 fps in phone emulation. That is the software rasteriser's
speed, not the game's.

The sim's share moves with the animals near the player (the population
director spawns and retires them), so it differs between runs.

Wasm: 2.11 MB (0.86 MB gzipped) before, 1.99 MB (0.80 MB) after. Loading,
from the page opening to the first title frame, measured the same way:
the CPU work before the first frame (world, terrain, ground materials,
plants) went from 11.4 s to 9.1 s, most of it the world and its colliders
(3.7 to 2.4 s, LTO). The title appeared at 38.7 s before and 19.0 s after
in this run (14.6 s after on the phone page, 20.6 s before), but most of
that is SwiftShader drawing the first frames, which the old fence waited
for one by one; on a real GPU expect the 2.3 s of CPU.

### What still dominates, and next

On the CPU, the shadow casters (1.2 to 2.2 ms: per cascade, a cull over every
tree, prop and plant in the cascade, then a list per mesh), then the sim and
bots, the HUD, the plant draws and props. Next:
- **Shadow casters.** Cache each cascade's caster lists while its fit has
  not moved (the cascades already skip re-rendering then), and draw the last
  cascade's tree cards from the static impostor buffer as the view does.
- **Draw calls.** About 450 to 500 in play: per mesh and LOD for plants and
  props, in the view, its depth prepass and three cascades.
  `WEBGL_multi_draw` would merge each pass into one call per program.
- **The GPU itself.** Not measurable here (WebGL hides timer queries, and
  SwiftShader is software). On the GTX 1060 the low preset renders 1058x556;
  the native `CRET_GPU_PROFILE=1` per-pass GPU times at that size are the
  reference. The knobs below trade GPU time.

### Knobs

Page parameters: `?preset=low|medium|high`, `?env=` with any of:
`CRET_RENDER_SCALE` (0.3 to 1; low is 0.7), `CRET_SHADOWS` (0 off, 1 three
1024 cascades, 2 four 2048 ones), `CRET_CLOUD_QUALITY` (0 to 1; low is
0.35), `CRET_GRASS=0`, `CRET_VEG_NEAR` (tree mesh range, m), `CRET_CARPET`
(sward range, m; 0 off). For measuring: `CRET_GPU_PROFILE=1` with
`CRET_PROFILE_EVERY=N` prints the per-pass CPU report every N frames to the
console; `PROFILE=1 tools/build_web.sh` keeps wasm function names for the
browser's profiler (`web_smoke.js --perf --cpuprofile --only=ferns`).

## Checking it

```bash
node ocean/cretaceous/tools/web_smoke.js --phone     # phone held sideways: taps, stick and camera together, rotation, menu by tap; memory (about 15 min)
```

```bash
node ocean/cretaceous/tools/web_smoke.js             # desktop: keyboard into play; memory and frame time (about 15 min)
```

```bash
node ocean/cretaceous/tools/web_smoke.js --short-press   # desktop: every click and key 10-30 ms long, on the title and in play (about 20 min)

node ocean/cretaceous/tools/web_smoke.js --perf      # frame time: per-pass CPU, WebGL calls, draws and uploads at the title and two play spots (about 40 min)
```

```bash
node ocean/cretaceous/tools/web_smoke.js --settings  # first-visit quality, settings and a character across a reload, menu arrows, Alt walk, browser keys (about 40 min)
```

```bash
CRET_TOUCH=1 ./build/cretaceous/cretaceous play --size 844 390   # the touch layout natively, at phone size
```

What `--phone` does:
- Emulates Chromium's mobile mode (touch, DPR 3, coarse pointer) with S3TC hidden, as on a phone's GPU.
- Taps Play on the title with a tap shorter than a frame.
- Holds the stick pushed to sprint while a second finger turns the camera, lifts the camera finger first, and checks that the animal kept moving.
- Turns the window upright and back, checks the canvas size and that the controls still show.
- Opens the menu with its button and resumes by tapping Resume.

It saves `phone_title.png`, `phone_play.png`, `phone_move.png`, `phone_upright.png`, `phone_menu.png` and `phone_resumed.png`.

What `--short-press` does:
- Sends every click and key as a down and an up 10-30 ms apart, each after a pause of up to a frame, so most fall inside one frame and some share one. A SwiftShader frame takes seconds, far slower than 10 fps, so nothing is throttled.
- On the title it clicks Settings, presses Esc back, Up five times to Play, then Enter. Play starts only if all of them counted, in order.
- In play it sends left and right clicks, Space (twice within a frame too), B, 1-3, E, G and Q. It holds W for three frames with key repeats, and holds W and the left button while the window loses focus.
- It presses Esc to the menu and back. It then ends the pointer lock as a browser does on Esc three ways: alone, with the browser also delivering that Esc, and with the page losing focus at the same moment.
- `touch.c` records the keys and buttons raylib reports down or up in each frame (`Module.cretInput`). The test fails unless every press was down for exactly one frame, in the order sent, and nothing stayed down.

It saves `web_title.png` and `short_play.png`.
