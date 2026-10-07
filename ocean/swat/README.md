# SWAT: Gold Element

A standalone tactical game within Ocean, with environment/config name `swat`.
Human play defaults to Cedar House: a small hostage mission with planning,
equipment, arrests and layered construction. Planning also offers generated
residences, two controllable snipers, and local house comparisons. The training annex remains the
stable policy/test environment. The longer-term
game is a cooperative tactical shooter with trained RL actors; see the
[development roadmap](ROADMAP.md).

This is an early graybox foundation. Armed guards use scripted sight and
hearing reactions, and civilians stay in place and can comply. Native training and checkpoint playback
work, but no capable trained opponent or squad policy ships with this commit.
The house generator does ship a small trained neural policy; it is separate
from character AI and has learned authored layout scores, not player enjoyment.
Surrender, restraint and less-lethal tools work; full policing, suspect behavior,
evidence and rules-of-engagement systems still need development.

## Play

Run commands from the repository root. With Box3D and Raylib installed:

```sh
make -C ocean/swat viewer
./swat play
./swat play --mission motel  # Briar Court, authored rooms/reception/laundry
./swat play --mission storefront  # Morrow Block, laundromat/pawn shop/rear service
```

The standard build entry point also creates the game and launcher:

```sh
bash build.sh swat --cpu
./swat play
```

`./swat` opens a main menu with solo play, **Settings**, **Host co-op**,
**Join co-op**, and **Quit game**. **Plan the mission** opens Briefing, Loadout,
Snipers and Houses tabs; **Deploy** captures the mouse. **Escape** or **Tab** opens
the pause menu, releases the cursor, and freezes the solo simulation. Resume captures
it again; losing window focus automatically pauses. The pause menu also offers
restart, settings, return to the main menu, and quit. Menu clicks and capture
warps are discarded before accepting gameplay input.

On WSL, the launcher uses the **native Windows player** with the same Raylib
and Box3D code. This avoids the WSLg/RDP pointer path implicated in
[reported mouse lock and relative-motion problems](https://github.com/microsoft/wslg/issues/240).
The first Windows build is automatic if needed, or can be run explicitly:

```sh
bash ocean/swat/build-windows.sh
./ocean/swat/play.sh play
```

The script uses an installed MinGW cross-compiler or downloads and unpacks
Ubuntu/Debian tool packages under `build/swat/windows-deps`, without sudo or
system installation. It fetches the official Raylib 5.5 Win64 archive and
builds Box3D into a separate Windows build directory. The player and a
`Play SWAT.cmd` launcher live in `build/swat/windows`. Only generated files
are placed there. On non-Debian Linux, install MinGW-w64 C/C++ first.

To explicitly run the Linux player, use `SWAT_NATIVE_WINDOWS=0 ./swat play`
or `./build/swat/swat play`. CUDA training continues to run on Linux. The
launcher rebuilds stale player code and translates checkpoint/settings/capture/layout-model
paths from WSL for the Windows process.

The launcher prints whether it selected Windows or Linux. WSL detection also
uses the interop registration when a shell has lost `WSL_INTEROP`. The Linux
fallback selects `GALLIUM_DRIVER=d3d12` when `/dev/dxg` is present and no renderer
override was supplied. Explicit `GALLIUM_DRIVER`, `MESA_LOADER_DRIVER_OVERRIDE`
and `LIBGL_ALWAYS_SOFTWARE` values remain authoritative. Check Raylib's startup
`Renderer` line: `llvmpipe` means software rendering. On this GTX 1060 workstation,
the initial generated-house benchmark measured 5.5 ms/frame for native Windows
and 190 ms/frame for llvmpipe; those are controlled benchmarks, not a guarantee
for every mission or WSLg session.

For a repeatable cost breakdown without the player's frame limiter:

```sh
make -C ocean/swat performance-tool
GALLIUM_DRIVER=d3d12 ./build/swat/performance_tool --generated --audio --frames 600
./build/swat/windows/performance_tool.exe --generated --audio --camera --frames 600
./build/swat/performance_tool --headless --generated --frames 600
```

To reproduce indoor wall-hit stalls, use `--indoors --fire --frames 180`.
`--indoors` starts at the first room centre; `--fire` continuously fires the
carbine and reloads it. Compare `--headless` with a rendered run and with
`--audio` to separate simulation, graphics, and output audio. `--audio-stereo`
keeps output audio while disabling the optional HRTF backend. `--hidden` runs
rendered Windows measurements without taking focus. `--storefront` and
`--motel` select the imported environments. Replay recording excludes
`--indoors`/`--fire`, whose starting-state overrides are not in replay headers.

The indoor shooting bottleneck was NPC acoustic routing: unheard events remain
pending for two seconds, and every route scanned all building pieces. Routing
now builds a query-local spatial tree over the acoustic bounds, shares it across
pending hearing events, and prunes doorway routes whose unoccluded gain cannot
beat the best path. It preserves ordered material sums and immediately observes
moving doors and destruction. Exhaustive comparisons cover 720 paths and a
180-tick hearing sequence with movement, mutations, arrival delays and log wrap.
On this workstation, adjacent native Windows headless tests (Cedar House,
1,109 pieces, nine actors, 30 indoor shots) reduced mean simulation time from
54.3 to 7.8 ms; the rendered/audio run averaged 17.6 ms/frame. Background load
and scene complexity still affect these measurements.

The tool reports simulation, audio, draw submission, presentation and total
mean/p50/p95/maximum times after warmup. `--camera` actually assigns a sniper and
renders its live feed; `--record FILE.sgrp` includes replay digest/write cost;
`--draw-only` excludes simulation, and `--plan` measures the cutaway. Use
`--expanded-camera` to measure the large takeover. The compact feed uses a
512 × 288 target; expansion restores 1024 × 576 with matching reticle proportions.
Use
`--capture FILE.png` for a final scene capture. **F3** shows FPS/frame time as
small debug text in play.

Lighting uses linear material shading, a free CC0 HDR sky with convolved diffuse
illumination and roughness-filtered reflections, warm room lights and continuously
weighted depth shadows. The visible sky, outdoor reflections and sun direction
come from the same source. Geometry changes invalidate the
shadow cache immediately; moving silhouettes refresh every four simulation
ticks.

Indoor ambient now darkens near adjacent floor/wall/ceiling planes, excluding a
surface's own plane. This room-bound approximation affects diffuse ambient and
indirect reflections; direct sun/lamp light keeps its existing shadow visibility.
Room lights use a less yellow neutral-warm tint. It is an inexpensive structural
approximation. An experimental half-resolution camera depth pass adds contact occlusion around
nearby world surfaces, recomputed after geometry changes without physics queries.
It affects indirect light only; first-person weapons and small camera feeds omit
this pass. Local scene reflections and global illumination remain future work.
`SWAT_IBL=0 ./swat` compares the previous analytic sky. Contact occlusion is
opt-in with `SWAT_CONTACT_SHADOWS=1 ./swat` because its extra geometry pass still
costs too much on the GTX 1060. See the [lighting source and bake notes](assets/environment/lighting_v1/README.md).
Original character specular/gloss maps now separate cloth, gloves and hard gear
instead of assigning the entire body one rough finish; see [CHARACTER_ART.md](CHARACTER_ART.md).
Frame limits are applied on change, eliminating the per-frame timer-log spam.

Every room has six stable 384 px shadow views covering every direction around
its lamp. Two guard texels and face-local receiver gradients keep filtering
continuous across view boundaries. Architecture depths are cached per
room and refreshed when nearby doors or geometry change; moving silhouettes are
composited over those depths every four ticks. Walking between rooms or backing
away no longer switches neighboring room shadows off. Room bounds still limit
each light's reach. This remains an authored preview lighting model,
with no baked global illumination or physical fixtures yet. Both immediate
geometry and imported door/prop models receive lighting. The HUD remains
unmodified. For comparison, `SWAT_LIGHTING=0 ./swat play` restores unlit shading;
`SWAT_EXPOSURE=0.8 ./swat play` adjusts exposure (0.25–3, default 1.1).
Walls now default to Sunder's restrained painted plaster, with separate pine
floor, framing, sage door paint and door wood materials. Their 512 px basecolor,
OpenGL normal and linear roughness maps use authored metre scales. The original
door geometry/hardware remains; its paint/recess UVs are projected in metres.
`SWAT_ENVIRONMENT_PBR=0 ./swat` selects the new basecolors without normal/specular
shading; `SWAT_ENVIRONMENT_STYLE=legacy ./swat` restores the previous material set.
The provisional imagegen plaster remains preserved with exact prompt and source
hash in `assets/environment/painted_plaster_v1.json`.
`SWAT_PLASTER_STYLE=weathered ./swat play` selects the preserved worn source map.
The local carbine loads its private rigid export with original 4K material maps;
[WEAPON_ART.md](WEAPON_ART.md) records setup, measured clearance and remaining fit work.
`SWAT_WEAPON_ART=0 ./swat` provides a procedural comparison.
After source edits, the native launcher builds only the app being launched.
`bash ocean/swat/build-windows.sh` still builds the full set of tools and checks;
`--target player`, `server` or `character` builds a selected app.
`./swat character --asset /path/to/private/character.glb` opens the independent
full-influence art preview, with original timing and no gameplay event commits.
[CHARACTER_ART.md](CHARACTER_ART.md) documents controls, measured parity, cost
and the live character implementation. The ignored Ready/walk install also
drives carbine squad bodies, first-person arms and reloads automatically in
`./swat`, preserving every influence on the GPU. Original body maps, achieved
stance/weapon fitting and shared camera/shadow poses are implemented.
These overrides are forwarded across WSL interop, with paths translated where
appropriate. Source wood/plaster tiling now follows the supplied metre scales.
[ENVIRONMENT_MATERIALS.md](ENVIRONMENT_MATERIALS.md) records role bindings,
channel conventions, resource ownership and validation limits.
`test_lighting` is an explicit graphics check requiring a display; it verifies
all six lamp directions and a face seam, room cache invalidation, moving
silhouettes, rotated/scaled mesh equivalence, world
immutability, exposure, opt-out and GPU resource lifecycle.

Room 101 uses eight v4 render replacements over the v3 room: bed, folded linen,
bedframe, sink, back wall, bathroom partition, carpet and window/curtains.
Original instance transforms, collision and removal ownership are preserved.
`SWAT_MOTEL_ROOM101=3 ./swat play --mission motel` compares the v3 art under
the same corrected lighting; `2` selects v2 and `0` the original bank.
Missing or invalid v4 assets fall back to the complete v3 set.
See the [native review](art_handoffs/room101-realism-v4/engine/README.md).

All four motel guest rooms now include a luggage rack and wastebasket at their
authored metre scale, with open mesh collision and replicated ownership. Their
separate AO UV set is supported by the material shader. Existing door/walking
routes and original motel instance IDs are preserved; these props are static
furniture for now. See the [integration and captures](art_handoffs/motel-utility-v1/engine/README.md).

At Cedar House, enter through doors or create openings, secure two suspects,
order the three civilians to comply, cuff them, and bring all surviving officers
back to staging. Any civilian harm fails the mission. Human play defaults to
five minutes. **P** opens planning: orbit the roof cutaway with A/D or inspect
three authored overwatch viewpoints. The overview shows geometry, not hidden
actor positions; optical previews show only what the camera can see through
the actual building. The overview is a planning camera. A separate deployable drone now has slow physical flight and its own live feed.

In **P > Snipers**, select A or B, choose an unoccupied post and a Precision or
Marksman rifle, then assign. **Take scope control** deploys queued assignments.
During play, a small live camera previews the selected sniper while your officer
moves. Hold **Tab** to free the pointer: A/B switches feeds, the close button
hides the camera, and **Assign sniper** deploys an unassigned unit at its selected post.
Click the image or **Take over** to expand it into a centered floating scope over
the dimmed, still-visible officer view. The mission keeps running while the
pointer is free. **N** hides/shows the camera, **Comma / Period** selects the
previous/next feed during ordinary officer play, and
**Enter** takes control or returns. Co-op teammates can preview; the leader controls.
Compact previous/next text controls appear beside A/B while Tab is held. Cycling also reopens a hidden inset.
**Page Up / Page Down** and **Backslash / Slash** are previous/next aliases;
the older left/right bracket bindings also remain available. Camera cycling
does not change the officer's weapon.
In the scope, mouse aims, **Y** marks an optically visible armed target, **Space**
orders both snipers to execute their marked shots, **H** clears marks, and
**LMB** fires the controlled rifle. **1/2** switches snipers; **Esc** returns to
your officer and the small live feed. From the officer, **X** executes and **H** clears. Snipers hold
fire until ordered; opaque cover and friendly/compliant people block a marked
shot. Wait for **TARGET READY**: the rifle must finish equipping and steady its
aim before a shot can fire. Glass uses the shared bullet penetration rules. Repositioning takes
three seconds and preserves wounds/ammunition; it currently moves the actor
to the new post after that delay. Your officer remains in the world while you
control a sniper, and online planning does not pause the mission.

Choose a kit before leaving staging, firing or using a consumable. Recon carries a carbine/optiwand
at full pace; Control trades some mobility for an impact launcher, optiwand and
light torso protection; Entry has a ram and stronger torso protection at a
larger movement cost. Control and Entry have gas masks. All have a sidearm and cuffs. The impact launcher forces
NPC surrender on a hit, but still causes injury and does not penetrate cover.
**F** uses a nearby door or requests compliance from people in a visible forward
cone. **Middle mouse** is another use/compliance binding; **Y** remains a
dedicated compliance alias. Healthy armed suspects generally need to be stunned
or wounded first. Hold **RMB** within 1.7 m while aiming at a compliant person
for 1.2 seconds to restrain them. Release or lose the target to interrupt.
One short floating text prompt shows the selected action and when to move closer. A
three-degree ray fan tolerates small reticle errors, with every ray stopped by
real cover. Physical reach limits remain enforced by authority.

Right-click chooses cuffing, picking or aiming when the press begins and keeps
that choice until release. Finishing a tool does not turn the held click into
aiming or firing, and another person cannot inherit the same cuff press. Away
from an available close tool action, **RMB** aims. Held **Z** always offers aim
without selecting a contextual tool.

Hold **G** near a closed door: the officer crouches and inserts the lens through
the floor gap automatically. Mouse movement rotates the lens without moving its
stem. While holding G, **Ctrl/C** selects under-door, **Q/E** reaches around a
left/right corner, and **Space** reaches over cover. The HUD shows the selected
mode and whether reach is blocked. The lens sweeps against collision; movement
and firing are locked during inspection. Release G to return to normal controls.
Use B for a butt strike or the Entry kit's stronger ram hit.

House exterior doors start locked. Hold **RMB** (or **L**) within 1.7 m while aiming at the
closed leaf to pick its lock for three seconds. Letting go, losing the door or
being interrupted resets progress. Picking leaves the door closed; **F** opens
it afterward. Picks are reusable in every kit. Hold **7** for 1.5 seconds to mount
a breaching charge to a fully closed door, then retreat and press **K** to
detonate your mounted charges. Another officer cannot fire your remote. The
charge consumes stock when mounting finishes, removes the actual door collider,
emits a material-aware blast sound, and can stun/injure nearby people through
the opening. Intact walls block exposure. Mounted charges appear on the leaf
and the HUD counts your remaining/mounted stock. Used tools cannot be refilled
by swapping kits at staging. The annex's training door remains unlocked.

**4** throws a flashbang and **5** throws CS gas. Canisters are dynamic Box3D
bodies with gravity, swept collision and material-dependent bounce/friction.
A 1.5-second fuse starts when thrown; holding a key throws once. Flash exposure
depends on distance, facing and cover. Gas grows for twelve seconds, respects
geometry, slows unmasked officers and can force NPC compliance. **T** fires the
limited-charge taser within 7 m through a checked short trace. It stops at cover,
stuns and can force surrender; it does not simulate a cable or flying darts yet.
These are fictional, non-damaging game effects; cuffs remain necessary.

| Kit | Flashbangs | CS canisters | Taser charges | Breaching charges | Gas mask |
| --- | ---: | ---: | ---: | ---: | --- |
| Recon | 1 | 1 | 2 | 0 | No |
| Control | 1 | 2 | 3 | 1 | Yes |
| Entry | 2 | 1 | 0 | 2 | Yes |

The [equipment comparison and gadget shortlist](EQUIPMENT.md) records SWAT 4,
Elite Force/First Responders, Ready or Not, and manufacturer sources for the
next equipment choices.

In **P > Houses**, choose a seed, difficulty and Learned/Random generator, then
**Build this seed** or **Next house**. Accepted layouts have connected rooms,
clear door frames, reachable occupants and a checked collider budget. The three
arrangements have 3–5 rooms; difficulty places 1–3 suspects and three hostages.
After playing two different houses, the comparison buttons save your explicit
choice locally beside settings as `settings.ini.layouts.jsonl`. Nothing is
uploaded, and votes do not retrain the running game automatically. See
[generation, model training and player preferences](GENERATION.md).

```sh
./swat play --mission generated --layout-seed 42 --difficulty 1
./swat play --mission generated --generator uniform
./swat play --mission generated --layout-model build/swat/layout-experiment/policy.txt
```

To revisit the annex or practice without hostile fire:

```sh
./swat play --env.hostile_fire=0 --env.max_ticks=7200 --env.randomize=0
./swat play --mission annex
```

| Control | Action |
| --- | --- |
| WASD / mouse | Move / look |
| Q / E | Hold left / right lean |
| Ctrl or C | Hold crouch; standing waits for clearance |
| Shift / Alt | Sprint / slow walk |
| Space | Jump; release before jumping again |
| Right mouse | Hold to cuff a close compliant person or pick a close locked door; otherwise aim |
| Z / left mouse | Hold aim / fire |
| R | Reload |
| 1 / 2 | Kit primary / sidearm |
| V | Cycle selector; carbine starts in semi, then auto, then safe |
| F / middle mouse | Use a door within 2.2 m; otherwise request compliance in front of you |
| Y | Dedicated compliance request |
| G | Hold optiwand; auto under-door, mouse lens aim; Ctrl/Q/E/Space select reach |
| 4 / 5 / T | Flashbang / CS gas / taser |
| X / H | Execute marked sniper shots / clear sniper marks |
| B | Melee / Entry kit ram |
| L / 7 / K | Hold lockpick / hold charge placement / detonate owned charges |
| Comma / Period | Previous / next camera feed while moving; reopens a hidden inset |
| Page Up / Page Down | Previous / next camera feed aliases |
| N / Enter | Hide/show camera / take control or return |
| Held Tab | Free cursor for camera buttons without pausing when the inset is open |
| P | Briefing, equipment, sniper placement/control and generated houses |
| Backspace | Restart while playing |
| Esc | Pause/resume; return from floating scope or menus |

The [SWAT 4 publisher manual](https://sierrachest.com/gfx/games/SWAT4/box/SWT4_Mn_TX_7162010.pdf)
describes contextual use, viewport cycling and a right-click command interface;
selected tools use the fire action. This prototype keeps the combined
use/compliance idea and adds direct contextual RMB cuff/pick access, alongside
the requested held-Tab camera pointer. Hold **M** for Gold/Red/Blue squad orders; Shift queues an order and **J** executes it. Orders affect scripted bots, never human peers.
The gameplay HUD follows the compact text and video treatment visible in
[Ready or Not screenshots](https://www.spaziogames.it/recensioni/ready-or-not-recensione)
and [SWAT 4 screenshots](https://www.play-asia.com/swat-4-gold-edition/13/70dsxt):
small edge status, a short action prompt, and camera controls when needed.
Roboto text uses a fine shadow for contrast. Interaction progress is a thin line;
the camera has a one-pixel edge, with no decorative frame or surrounding card.
Unassigned/down cameras collapse to text. Holding Tab shows camera controls and
equipment counts; the permanent full-width control footer is gone. Planning,
settings and pause retain their dedicated menus. The earlier
[generated UI experiment](assets/ui/README.md) is preserved in source.

## Co-op and self-hosting

Host or join through the menu, or from the repository root:

```sh
./swat host --port 27474
./swat join 192.168.1.50 --port 27474
make -C ocean/swat server
SWAT_NATIVE_WINDOWS=0 ./swat server --port 27474
```

Four officers share server-owned movement, weapons, doors, damage, destruction
and mission outcomes. All surviving officers must extract. Only the leader can
restart, choose a house or command snipers. Online pause/settings release your controls while the session continues.
Solo/offline play remains available. Planning also releases controls online;
the session continues. The default hosted mission is Cedar House, five minutes;
`--mission annex` selects the old range. Hosts and peers must use protocol v8.

The dedicated server has no display, Raylib, audio-device or CUDA dependency.
On WSL, `./swat server` defaults to `build/swat/windows/swat-server.exe`;
the example explicitly selects Linux. Server options include `--seed 42`,
`--mission house|annex|generated`, `--layout-seed`, `--difficulty`, `--generator`,
`--layout-model`, `--max-ticks 18000`, `--hostile-fire 0`, `--randomize 0` and `--help`.

Connections currently use direct IPv4 addresses/DNS and UDP port 27474.
Internet hosting requires a reachable UDP port and appropriate router/firewall
configuration. There is no lobby service, NAT traversal, relay, authentication,
encryption or host migration yet. Movement uses authoritative snapshots;
high-latency presentation still needs prediction/interpolation. See
[architecture and next priorities](ARCHITECTURE.md).

When mixing native Windows and a WSL Linux server, use the WSL interface IP
and Windows host gateway IP respectively; UDP loopback forwarding is not
assumed. The default native Windows host/join path avoids that extra boundary.

The graybox weapon model, actors, and HUD use procedural Raylib geometry.
No assets from Ready or Not or SWAT 4 are included.

## Saved settings

The Raygui settings page has sliders for mouse sensitivity, vertical
sensitivity, aiming sensitivity, vertical FOV, frame limit, and master volume, plus independent
horizontal/vertical inversion. The default mouse sensitivity is **0.035 degrees
per pixel**, about one third of the original setting, with a 0.65 aiming
multiplier. Positive horizontal motion turns right; moving the mouse up looks
up unless inversion is enabled. Mouse displacement is independent of frame rate.

**Escape → Settings → Weapon view** opens a small panel on the left with a live
preview of weapon size, horizontal position, vertical position and ADS eye
distance. Switch the preview between hip and aiming without changing gameplay.
**Reset view** resets these four values. The default projects the weapon at 170% of the
old size (62° weapon FOV becomes about 38.9°), with a small height/position
correction. This changes first-person presentation only: world weapon dimensions,
reach and firing remain measured. Hip-view offsets fade out while aiming, and
aimed sights retain their original alignment. Carbine ADS defaults to 120 mm
between eye and rear aperture, adjustable from 80 to 220 mm. The entire gun and
arms move together; normal crosshair lines fade away so the post remains visible.
Preferences support `weapon_size`, `weapon_horizontal`, `weapon_vertical` and
`weapon_ads_relief`; size is magnification and the other values are metres.

**Save & back** writes preferences immediately; Escape saves edited values
before returning. **Reload saved** restores the file, and **Defaults** restores
the starting values. Changes also save on orderly exit. Files are versioned,
validated, and replaced atomically; a failed save keeps the previous file and
shows an error in the menu. Preferences load at startup from:

- Windows: `%LOCALAPPDATA%\SWAT Gold Element\settings.ini`
- Linux: `$XDG_CONFIG_HOME/swat-gold-element/settings.ini`, falling back to
  `~/.config/swat-gold-element/settings.ini`

Use `--settings /path/to/profile.ini` for an independent profile. The user
preferences file stores controls/display settings. Gameplay/mission and policy
configuration continue to live in `config/swat.ini`; preference changes do not
change the RL action/observation contract. Human FOV is adjustable; policy
playback retains its sensor FOV. Raygui is already vendored at
`vendor/raygui.h`; no separate GUI library installation is required.

## Implemented systems

- **Character:** dynamic Box3D body, separate feet and upper capsule, normalized
  movement, stair/ground handling, crouch clearance, grounded jump, pitch limits,
  walk/slow/sprint, stamina, and movement/aim/fire gates. Lean shifts the actual
  upper collider and eye by up to 0.42 m; it is swept against cover. Standing
  checks the leaned head as well as the feet hull. Aim, roll, and sensor rays
  follow the achieved pose.
- **Weapons:** carbine, sidearm, impact launcher, two sniper rifles, pepper launcher, breach shotgun and compact SMG profiles; chamber plus magazine/reserve counts,
  semi/auto/safe selectors, timed tactical/empty reloads, reload cancellation on
  swap, equip delay, recoil, movement/air/aim spread, and deterministic actor-local
  weapon RNG. Reloads commit magazine removal, insertion and chambering separately.
  Cancelling or switching preserves committed ammunition and can leave the
  magazine detached. Staging offers pooled reserve or retained spare magazines,
  irons/red dot/optic sights, and primary profiles. Eye, sight, hands and muzzle
  share a canonical pose for rendering and clearance. PepperBall, impact/CS/flash, probe and tether profiles use physical
  rounds. The breach shotgun uses the shared traced damage/penetration rules.
- **Hits and cover:** eye-to-muzzle volume check, muzzle-origin hitscan, nearest
  collision damage, bounded thickness/material penetration, and head/torso/arm/leg
  damage. Leg wounds reduce pace, arm wounds widen spread, and plates reduce
  torso damage. Cedar House uses two thin board/plaster faces, a cavity, timber
  studs, plates and headers; breaking a face exposes what remains behind it.
  Supports are fixed rather than a structural collapse simulation. Destroyed objects lose
  their physics colliders and disappear from sensors immediately. This is
  modular destruction, not structural fracture or simulated debris.
- **Doors:** an authored hinged, damageable door with real collision. Its short
  swept rotation stops for actors and resumes once they clear it. It currently
  moves under game control rather than a motorized rigid-body hinge.
- **Mission:** house suspects/hostages, command/compliance, hold-to-cuff arrests,
  protected civilians, secure-and-extract success,
  injury/death, civilian-harm failure, fall/timeout, restart, and episode metrics.
  Annex variations, authored Cedar House and neural/uniform generated houses.
- **Materials and equipment:** shared density/friction/restitution/rolling values,
  CCD canisters, cover-sensitive flash/CS exposure, masks, finite taser/throwable
  stocks and collision-limited optiwand placement with independent lens aim.
- **Audio:** shared material/thickness/doorway propagation, delayed directional
  hearing, listener/source room reflections/tails, optional Steam Audio HRTF, procedural
  shots/steps/handling/doors/impacts/breakage and guard turning toward audible
  cues. Enable the local runtime with `python3 ocean/swat/setup_audio.py`.
  Production source recordings and richer propagation still need work; see
  [research, setup and listening comparisons](AUDIO.md).

The carbine has a 30-round magazine plus chamber, 90 reserve rounds, a 6-tick
fire interval, 120/156-tick tactical/empty reload, and 24-tick equip delay.
The sidearm uses 15 plus chamber, 45 reserve, 10-tick fire interval,
90/120-tick reload, and 18-tick equip delay. These are fictional game tuning.

## Shared simulation

| File | Responsibility |
| --- | --- |
| `body.c`, `body.h` | Local low-level character fork and collision queries |
| `controller.c`, `controller.h` | Tactical input, stance/lean/aim, movement gates, recoil |
| `weapons.c`, `weapons.h` | Weapon state and shot requests |
| `world.c`, `world.h` | Box3D scene, queries, material damage, doors |
| `mission.c`, `materials.c`, `equipment.c` | Authored house/framing/viewpoints and editable material/kit tables |
| `tactical.c`, `overwatch.c` | Physical canisters/effects/taser and authoritative sniper orders/control |
| `generation.c`, `layout_tool.c`, `train_layout.py` | Validated layout grammar, C inference and offline model training |
| `feedback.c` | Explicit local comparisons between played houses |
| `sim.c`, `sim.h` | Actors, fixed update, ballistics, mission, observations/actions |
| `swat.h` | Ocean adapter, rewards, logging, automatic reset |
| `render.c`, `swat.c` | Game presentation, fixed update loop, CPU policy playback/evaluation |
| `frontend.c`, `settings.c` | Main/pause/settings menus, mouse capture, saved player preferences |
| `protocol.c`, `net.c`, `server.c` | Versioned codec, UDP co-op authority/replica, headless server |
| `acoustics.c`, `audio_dsp.c`, `spatial_audio.c`, `sound_view.c` | Shared hearing, room mixer, optional HRTF, device stream |
| `audio_lab.c` | Offline WAV/CSV acoustic comparisons |

Both humans and policies submit `SwatInput` to the same **60 Hz** game update
with **four Box3D substeps**. Rendering does not own physics or weapon state.
`swat_sim_step_inputs` accepts per-actor input for future policy-controlled
suspects and teammates. The initial Ocean adapter exposes only the officer.

The simulation is heap-owned because native vector setup may relocate `Env`;
Box3D actor/object metadata must keep stable addresses. World create/destroy
operations are serialized for the Box3D world registry; separate worlds step
on the vector workers. Headless evaluation opens no graphics window. The
adapter binary still links Raylib for its optional render hook.

The character ancestry and dependency revision are recorded in
[PROVENANCE.md](PROVENANCE.md). SWAT owns its controller fork and does not import
Shenaniguns gameplay code, assets, checkpoints, observations, or tasks.

## Environment art

The player now applies a small worn-plaster/timber pack and an imported door
leaf to the live generated geometry. Individual boards still disappear with
their own authoritative damage, and doors follow their real hinges. See
[the render-only integration and asset contract](ENVIRONMENT_ART.md) for the
scope, provenance, fallback controls and checks.

## Dependencies

Use the existing sibling Box3D checkout if it is already at this revision.
For a fresh dependency checkout:

```sh
git clone https://github.com/FelixAllistar/box3d.git ../box3d
git -C ../box3d checkout --detach c4a414fcfe612a704dcd06ce921348d441271fc7
cmake -S ../box3d -B ../box3d/build -DCMAKE_BUILD_TYPE=Release \
  -DBOX3D_SAMPLES=OFF -DBOX3D_UNIT_TESTS=OFF
cmake --build ../box3d/build --parallel 2
```

`build.sh` uses the repository's Raylib 5.5 download/setup path. The local
Makefile expects `raylib-5.5_linux_amd64` and Clang on Linux; override `RAYLIB`,
`BOX3D`, or `CC` when needed. `build.sh` accepts `BOX3D_DIR` for a different
physics checkout. CUDA and the repository's native trainer dependencies are
needed for training, not for the standalone player or core simulation tests.

ENet is vendored at its pinned 1.3.18 revision. A standalone CMake build can
build the server and core/network tests without the training repository's tools:

```sh
cmake -S ocean/swat -B build/swat/portable -DSWAT_BUILD_PLAYER=OFF \
  -DSWAT_BOX3D_DIR=/absolute/path/to/box3d -DCMAKE_BUILD_TYPE=Release
cmake --build build/swat/portable --parallel 2
ctest --test-dir build/swat/portable --output-on-failure
./build/swat/portable/swat_server --port 27474
```

Enable `SWAT_BUILD_PLAYER` and set `SWAT_RAYLIB_DIR` to build `swat_player`.

## RL contract and training

[CONTRACT.md](CONTRACT.md) defines both training contracts. `config/swat.ini`
selects the native trainer's task with **`env.task=movement`** (default) or
**`env.task=annex`**. Rebuild `puffer` after changing the task; the binary checks
that its compiled contract matches the config at startup.

The annex v1 interface has **167 float
observations**, **14 discrete action heads**, and **39 total logits**. The
observation combines character/weapon state, mission telemetry, and 45 forward
visibility rays. It does not include hidden enemy positions. Visible ray classes
are semantic labels, not rendered RGB; human and policy physics are shared,
while their observation modalities differ.

The baseline is the standard linear encoder and recurrent MinGRU policy with
width 64 and two layers. SWAT does not use the Shenaniguns spatial encoder.
Run the normal trainer from the normal repository checkout:

```sh
cd ~/puffertank/pufferlib
bash build.sh swat --float
./puffer train
./puffer eval latest --headless
```

Configuration lives in `config/swat.ini` and inherits `config/default.ini`.
`--section.key=value` overrides work for both the player and trainer. Use the
checkpoint's matching architecture and scenario settings. The viewer checks
weight count against the architecture; the raw native checkpoint does not
embed a SWAT contract identifier. Keep config/contract metadata with checkpoints.
The 64x2 policy has 27,840 FP32 parameters for movement and 37,824 for annex.
Checkpoints use the normal `checkpoints/swat/<run>/` location and effective
configs are saved under `logs/swat/`. Annex checkpoint playback remains available
through `./swat watch FILE.bin`; movement course playback uses `./puffer eval FILE.bin`.

The environment is CPU Box3D, with CUDA policy training. A separate CUDA
environment, BF16 qualification, self-play league, multi-agent adapter, and
production learning benchmarks are not implemented in this foundation.

## Verification

```sh
make -C ocean/swat test
make -C ocean/swat net-test
make -C ocean/swat sanitize
make -C ocean/swat audio-lab spatial-test
./swat --capture build/swat/first-playable.png --env.randomize=0 --env.hostile_fire=0
./swat --capture build/swat/settings.png --capture-screen settings
./swat --capture build/swat/house-plan.png --capture-screen plan
./swat --capture build/swat/overwatch.png --capture-screen overwatch
```

Checks cover movement speed and gates, jump edges, crouch/lean collision and
combined stand clearance, ammunition conservation and cadence, reload/swap,
muzzle obstruction, cover removal and penetration, door obstruction, and guard
visibility/reaction. A test-only driver completes eight randomized missions with
enemy fire enabled using ordinary movement, interaction, and weapon inputs.
It has known waypoints/target poses; it is a solvability check, not learned AI.

Co-op checks cover extraction/squad failure and guard targeting teammates.
Real UDP tests cover four players, capacity rejection, independent movement
and ammunition, shared door/destruction/audio state, late join, epoch resets,
leader authority, terminal menus, disconnect cleanup and listen-host shutdown.
Codec tests cover malformed/truncated packets, nonfinite fields and replica
colliders. Acoustic and PCM tests cover shared occlusion/timing and output.

House checks exercise independent board faces/studs, rotated door collision,
optiwand clearance, authority kit restrictions, impact rounds stopped by cover,
ram breakage, surrender, interrupted cuffs/fire isolation, civilian commands,
leg-injury mobility and secured-house extraction. This interaction driver uses
fixture placement to isolate rules; it is not a complete navigation policy.
Real UDP checks also transfer the full house and late-join wounds/restraints.
New checks cover canister mass/bounce/CCD, cover-sensitive effects, consumable
conservation, independent optiwand aim, sniper placement/mark/hold/execute and
friendly interlocks. Generated-house tests include 1,024 deterministic plans,
314 real standing door crossings across 24 houses, exact collider counting and
token/seed/model replication. Door checks cover aborted picking, finite mounting,
owner-only remote detonation, blast occlusion/injury and staging refill prevention.
UDP checks cover active gas and mounted charges at late join, shared breach
collision, and leader-only scenario changes. The optional Python smoke command exercises
preference reward training and policy updates using marked synthetic labels.
Native HRTF checks verify directional impulse differences; room mixer checks
verify longer decay retains more late energy.

Adapter checks cover 2,048 paired seeded transitions, 33 paired resets,
finite observations, relocated environment storage, exact timeouts, civilian
penalty, extraction reward, and preservation of terminal reward across reset.
ASan/UBSan instrument the game/controller and adapter, not the separately built
Box3D archive. Offscreen rendering has also been captured and visually inspected.

Settings tests cover save/load/replacement, preserving the old file after a
failed save, malformed values, unsupported versions, mouse directions,
inversion, and frame-independent sensitivity. The native Windows GUI regression
opens its own brief test window and checks the actual OS cursor confinement
rectangle, stationary mouse input, Escape/pause/resume/quit, focus loss,
click isolation, slider cancellation, host/join setup/cancel, leader restart
controls, active audio streaming, preferences surviving reinitialization,
automatic optiwand insertion, live camera keyboard/button cycles, floating
scope/cursor isolation, minimal text controls, F use/compliance, Z aim, held/aborted
RMB pick/cuff and completion click isolation, sniper mark/execute,
charge controls and local house comparisons:

```sh
bash ocean/swat/build-windows.sh
./build/swat/windows/test_frontend.exe build/swat/windows/frontend-test-settings.ini
```

It uses only the supplied test settings path. For native Linux, build the same
GUI test with `make -C ocean/swat frontend-test-build` and run
`./build/swat/test_frontend build/swat/frontend-test-settings.ini`. That Linux
test can inspect Raylib capture state; the Windows test additionally checks
the OS clipping rectangle. Interactive play on WSL uses Windows by default.

Separate-process probes also passed from native Windows to the WSL Linux server
and from Linux to the Windows server using their interface IPs. A joining native
client moved/fired against the actual listen-host player with the host window
minimized. These verify this machine's transport/UI integration; a separate
two-machine LAN session and adverse network conditions remain to qualify.

With `env.task=annex` and a matching rebuilt trainer, a native FP32 smoke run
completed 2,048 transitions with finite losses and
saved checkpoints; loading those weights into another 2,048-step training run
also passed. CPU deterministic evaluation loaded the result successfully;
the tiny smoke policy solved 0/4 short episodes. This verifies execution and
checkpoint compatibility, not policy quality. Reproduce a short run with:

```sh
./puffer train --vec.total_agents=16 --vec.num_threads=2 \
  --train.horizon=16 --train.minibatch_size=128 --train.total_timesteps=2048 \
  --env.max_ticks=32 --env.hostile_fire=0 --base.run_id=smoke \
  --base.checkpoint_dir=build/swat/checkpoints --base.log_dir=build/swat/logs \
  --base.checkpoint_interval=1
./swat --eval build/swat/checkpoints/swat/smoke/0000000000002048.bin 4 \
  --deterministic --env.max_ticks=32 --env.hostile_fire=0
```

## Controller range and deterministic replays

Launch `./ocean/swat/play.sh --mission range` for stairs, shallow/steep ramps,
crouch clearance, low cover, material targets and a framed door. **Home** and
**End** toggle low/high ready; aiming or firing raises the weapon. **F3** shows
physical bodies, eye/muzzle clearance and sensor rays. **Alt + Backslash**
interrupts a reload. Primary/sight/magazine choices are in the staging loadout.

Use `--record /path/to/round.sgrp` to capture a solo round from tick zero.
Build `make -C ocean/swat replay-tool` and run
`build/swat/replay_tool /path/to/round.sgrp` to verify each recorded authoritative
state. Native Windows also builds `build/swat/windows/replay_tool.exe`.
Files record the wire version, scenario settings, reset RNG seed, fixed-tick
inputs and portable snapshot hashes; incompatible/truncated files are rejected.
These are same-build solo replay checks, not full save games or a guarantee of
identical results with a different layout model or across architectures.

## Tactical encounter rules

Human house/generated play now enables three scripted squad bots in vacant
officer slots. Humans replace those bots when joining co-op. The legacy annex
and policy contract keep their previous defaults. Hold **M** for floating squad
orders; choose Gold, Red or Blue, Shift-click to queue, and press **J** to execute.
Orders acknowledge after 18 simulation ticks. Bots route through actual static
clearance, open/pick encountered doors and yield to nearby actors. This is an
initial authored tactical behavior layer; it is not a trained squad policy.

**Alt+F** peeks an unlocked door to 12 degrees. **9** places a wedge, **Alt+9**
recovers it, **8** disarms an inspected trap, and **6** sprays pepper. These are
held actions with finite stock and cover checks. **Alt+1/2** cycles the equipped
small tool; left click uses it, and plain **1/2** returns to a weapon. Wedges
block the same leaf for every actor. Difficult generated tactical houses may
have traps; inspect with the optiwand before disarming. Opening a trapped leaf
triggers an occluded flash/stun game effect.

Cuffed civilians can follow an officer via **F**, or hold position on another
press. Surrendered/down suspects leave weapon evidence; aim down and use **F**
to collect it. The end-of-round debrief records arrests, rescued civilians,
evidence and harm to civilians/surrendered/restrained people. Evidence and
rescue currently add recorded outcomes, rather than new mandatory victory
conditions. Suspects use visible targets and short-lived memory; noise supplies
a coarse bearing for investigation, never a hidden actor position.

## Remote devices and physical less-lethal rounds

Continue **Alt+1/2** through throwable camera, ground robot, communication ball
and drone; left click deploys the selected device. Each currently has one
finite inventory item. Comma/Period and aliases cycle deployed devices alongside
sniper A/B. **Enter** takes control of your own device; **WASD** drives a robot
or drone, **Space/Ctrl** raises/lowers a drone, and mouse turns its lens. The
officer stays still and vulnerable. **F** transmits compliance requests from a
communication ball; its link is audio-only. Audio follows the selected remote
location during takeover. Devices collide, emit noise, have battery/health
limits, and can be recovered with an aimed **F**. Thermal vision is not modeled.

Primary selection in staging includes separate physical impact, CS and flash
launcher profiles, a pepper launcher, a ten-probe CEW, and an experimental tether
restraint. The selected profile owns its ammunition; switching payload profiles
after using a weapon/tool is rejected. Pepper projectiles break into a small
occluded irritant cloud. Impact rounds injure by body region and can subdue.
A single probe is insufficient: two separated attached contacts and clear
tethers are required for a connection. Crouch/turn moves contacts with the body.
A tether restraint checks its hit region and creates temporary movement denial;
ordinary cuffs are still required. The original **T** taser remains the simple
profile; v1 annex impact handling remains compatible with old policy defaults.

### Solo mission checkpoints

Press **F5** to save the current solo mission and **F9** to restore it. The
checkpoint sits beside settings as `<settings path>.mission.sgrp`; a local
`<settings path>.live.sgrp` journal enables exact continuation. `./swat play
--resume FILE.sgrp` resumes a supplied checkpoint. Restore verifies the entire
input journal before replacing the live world, and subsequent F5 saves include
the continued mission. Load restores the saved kit, primary, sights, retained
magazines and squad state. This is solo persistence; co-op saves are pending.

Saves require this network/replay version (currently 9) and the same generated
layout model. Long journals take time to replay on load; there is no bounded-
time snapshot restore or crash recovery guarantee. Policy mode does not save.


## Scenario systems: buildings, wall breaches and movement

Scenarios define geometry, occupants, equipment and initial conditions. There
are no required action sequences, scheduled enemy waves or mandated entry tools.
The player chooses how to resolve the situation with the available tools.

```sh
./swat --mission building --layout-seed 7 --difficulty 1
```

The **Scenarios** planning tab can generate a building or another house from a
seed. Buildings have six rooms across two floors, a physical staircase, varied
room uses/furnishings, different exterior openings, and occupants on both floors.
The seed determines those initial conditions. Difficulty adjusts armed occupant
count. These are assembled prototype environments, not finished architectural art.

Hold **7** while looking at a destructible wall or door to mount a finite charge;
release or look away to interrupt. Move back and press **K** to detonate charges
you own. Wall charges remove a localized opening through both skins and the
segmented studs, with bounded shedding of unsupported adjacent skins. Geometry,
penetration, visibility, navigation and replicated collision share the convex
fragment shapes. Crack boundaries are irregular, while surface UVs remain
continuous in metres. Short-lived chips and persistent small rubble are cosmetic;
there is no full structural stress or dynamic rubble simulation yet.

Squad movement now routes by floor height, standing/crouching clearance and actual
stair support. Doors are operated on approach; actors still move through the real
controller rather than teleporting along the route. Wall destruction updates nearby cells and their connections in
the shared navigation graph. Acoustic spatial queries share an exactly validated
thread-local tree; moved doors and removed fragments refresh it immediately. The new scenario tests verify both human and squad
stair traversal, finite/interruptible charges and traversal through a replicated
breach. The network geometry contract is version **9**; version 8 clients and
journals are incompatible, so use a fresh recording for this build.

The audio bank contains 45 CC0 recorded clips with 67 event/material bindings:
carbine/sidearm shots, mechanical handling, footsteps and impacts. They use the
existing propagation, occlusion, directional audio and room decay mixer. Missing
bindings retain the synthesized fallback. Some material families currently share
recordings, and the reload mechanisms are airsoft recordings rather than a final
weapon-specific production selection. Attribution, source hashes and clip cuts
are in `assets/audio/SOURCES.json`. No licensed purchases are required.
`SWAT_SOUND_ASSETS` can override the bank directory. Rebuild the bank from the
archived sources using `build_audio_bank.py --source-dir ... --converter ...`.
`make -C ocean/swat audio-import` builds `build/swat/audio_import`, a Raylib-based
converter that opens no audio device or window.

### Movement training

Edit `config/swat.ini`; build with `bash build.sh swat --float` and run
`./puffer train`. There is one SWAT config, one normal Puffer binary and no
movement wrapper or separate environment name. Training uses CUDA for the policy
and CPU Box3D for simulation, with no Python training.

On WSL, SWAT's renderer defaults to `GALLIUM_DRIVER=d3d12` before opening its
window, including plain `./puffer eval latest`. Explicit graphics overrides
still take precedence; headless training does not initialize the renderer.

`env.task=movement` selects the 32-observation, six-action-head movement contract
(v2). `stage=-1` mixes goal approach, stairs, crouch clearance, doors and already-
breached walls; stages 0–4 select those individually. `role=-1` mixes officers and
suspects, or select 0/1. Episode length and progress, time, success and fall reward
coefficients are config settings. `env.seed` changes physical episodes and
`base.seed` controls trainer initialization. Evaluate on separate episode seeds.

```sh
./puffer train
./puffer eval latest --headless --env.seed=2000003
# Omit --headless to view the movement course.
./puffer eval latest
```

To start with a simpler curriculum and continue on stairs using normal overrides:

```sh
./puffer train --env.stage=0 --base.run_id=goal --train.total_timesteps=250000
./puffer train --env.stage=1 --base.run_id=stairs --base.load_model_path=latest
```

Weights use the native FP32 `.bin` PufferNet format, with a linear encoder and
recurrent MinGRU. Keep the effective config with each checkpoint. The old Python
MLP `.txt` weights are incompatible and removed. Changing movement/annex task or
architecture requires matching weights; the trainer rejects the wrong byte count.

The observations contain relative waypoint direction, velocity, stance, role and
local collision/floor probes, without hidden enemy positions. Actions choose turn,
forward/backward motion, strafe, crouch, jump and gait. Each decision runs for four
actual physics ticks in training and in the player. Every actor has its own
recurrent state, reset between scenarios. Navigation supplies waypoints; orders,
doors, weapons and rules of engagement remain game systems. Civilians retain their
scripted movement. This curriculum does not train combat or squad tactics.

Learned movement remains opt-in in the game:

```sh
./swat --mission building --locomotion-policy checkpoints/swat/<run>/<step>.bin
```

Use an actual checkpoint path from your run. The player reads architecture from
`config/swat.ini`; default NPC movement remains the height-aware navigation. The
runtime checks exact byte/weight count and finite values before replacing a
loaded policy. `make -C ocean/swat movement-native-test` checks native inference,
actor recurrent isolation, episode reset and four-tick cadence. Native smoke runs
verify training, loading and Windows playback, not capable movement policy quality.

### Simulation performance and physics comparison

Movement courses allocate a navigation grid only if a route is actually requested;
ordinary movement training never needs it. Suspect courses contain the learner
alone. Their movement step shares the gameplay controller, door interaction,
collision world, 60 Hz tick and four solver substeps, while skipping unused combat,
device and footstep bookkeeping. Gameplay and the combat task use the full step.
Observation/action dimensions and existing movement checkpoints remain compatible.

The 7 October pass reduces `SwatSim` from 810,016 to 372,744 bytes: 854 MiB less
reserved across 2,048 courses. A 4,500-decision before/after trace exactly matches
observations, rewards and terminals; a further 6,000 decisions compare the movement
step with full gameplay stepping. Navigation allocation, reset, map replacement,
door/breach routing and cleanup have separate regression coverage.

Two repeated benchmarks visiting every world each decision averaged **15,986 to
20,008 decisions/s** (+25%). A short native Puffer job with the existing 2,048
environments and horizon 64 finished in **57.23 to 43.10 seconds**, with peak
process RSS **2.27 to 1.43 GiB**. These short local runs include startup and have
WSL/background variability. A 512-environment/horizon-256 trial reduced memory
further but had slower rollout intervals, so the user's batching is retained.
Raw results are in [the movement iteration report](benchmarks/movement-iteration-2026-10-07.json).

Character clearance casts exclude the querying actor in the broad phase, while
physical collision masks continue to include walls, other actors and projectiles.
Movement training skips automatic door targeting when there is no closed door in
reach. It retains the original cursor cone and occlusion checks when a door can be
reached. Physics timestep, solver substeps, grenade handling and observation format
are unchanged. A 7,500-decision before/after trace matched exact observations,
rewards, terminal results and serialized game snapshots; another 1,000 clearance
sweeps matched the previous callback filter exactly, including other actors.

In the earlier 6 October pass on this four-core WSL workstation, two adjacent movement-simulation benchmark
runs averaged **24,939 decisions/s before and 33,551 after** (35% higher throughput).
These measurements include sensors and episode resets, but exclude the neural
policy, rollout transfers and PPO updates. They are not complete training SPS.
That older benchmark stepped each world repeatedly while its data stayed in cache.
The benchmark now defaults to `--schedule rollout`, visiting every world on each
decision; `--schedule env` reproduces the older scheduling for historical comparisons.
Your normal `bash build.sh swat --float` / `./puffer train` uses these optimizations.
No config change or alternative trainer is needed.

The native simulation benchmark is optional:

```sh
make -C ocean/swat training-benchmark
./build/swat/training_benchmark --envs 32 --threads 4 --steps 1500
# Single-worker trace for testing behavior across code changes:
./build/swat/training_benchmark --envs 1 --threads 1 --steps 1500 --trace build/swat/movement.trace
```

An optional C++ benchmark copies the five actual curriculum geometries into
Box3D and PhysX, with a driven character proxy, a CCD grenade sphere, 18 sensor
rays per decision and two clearance sweeps per physics tick. It does not implement
the SWAT controller in PhysX and does not select a different gameplay backend.
With 256 scenario copies and four workers, two repeated runs averaged **15,065
decisions/s for Box3D and 11,967 for PhysX CPU**. PhysX used its normal TGS solver
with four position iterations (internal temporal substeps), one velocity iteration
and one simulation call per 60Hz tick. Box3D used its existing four solver substeps.
The earlier four-explicit-call PhysX measurement performed additional TGS substeps
and is excluded from this comparison.

This collision workload provides no CPU throughput reason to port the game.
The untuned PhysX grenade traces also differ from Box3D; shared coefficient values
alone do not establish matching rolling resistance, contacts or controller behavior.
The raw repeated measurements and limitations are recorded in
[the CPU comparison](benchmarks/cpu-comparison-2026-10-06.json).

PhysX 5.7.0's CUDA context source requires SM 7.0, despite the GPU guide describing
Pascal compatibility. This workstation's GTX 1060 is SM 6.1. GPU comparison was
set aside; the game and trainer retain Box3D.

To rebuild the optional CPU comparison without Python or changing the normal
Puffer config, fetch the pinned SDK into the ignored build directory:

```sh
git clone --depth 1 --branch 109.0-omni-and-physx-5.7.0 https://github.com/NVIDIA-Omniverse/PhysX.git build/swat/physx-sdk
cmake -S build/swat/physx-sdk/physx/compiler/public -B build/swat/physx-build -G Ninja \
  -DPHYSX_ROOT_DIR="$PWD/build/swat/physx-sdk/physx" -DTARGET_BUILD_PLATFORM=linux \
  -DCMAKE_BUILD_TYPE=release -DPX_GENERATE_STATIC_LIBRARIES=ON \
  -DPX_GENERATE_GPU_PROJECTS=OFF -DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF \
  -DPX_OUTPUT_LIB_DIR="$PWD/build/swat/physx-lib" -DPX_OUTPUT_BIN_DIR="$PWD/build/swat/physx-bin"
cmake --build build/swat/physx-build --target PhysX PhysXExtensions PhysXCooking --parallel 2
cmake -S ocean/swat -B build/swat/physics-comparison -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSWAT_BUILD_PLAYER=OFF -DSWAT_BUILD_TESTS=OFF \
  -DSWAT_BUILD_PHYSX_BENCHMARK=ON -DCMAKE_C_FLAGS=-DB3_MAX_WORLDS=8192 \
  -DSWAT_PHYSX_DIR="$PWD/build/swat/physx-sdk/physx" \
  -DSWAT_PHYSX_LIB_DIR="$PWD/build/swat/physx-lib/bin/linux.x86_64/release"
cmake --build build/swat/physics-comparison --target swat_physics_benchmark --parallel 2
./build/swat/physics-comparison/swat_physics_benchmark --backend box3d --envs 256 --steps 150
./build/swat/physics-comparison/swat_physics_benchmark --backend physx --envs 256 --steps 150
```

Add `--trace FILE.csv --envs 1 --threads 1 --steps 30` to inspect a two-second
grenade trajectory. `--physx-substeps 4` requests four explicit calls per tick in
addition to TGS's internal substeps; use it only as a separately labeled experiment.
