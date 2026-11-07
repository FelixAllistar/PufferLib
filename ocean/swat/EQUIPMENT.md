# Equipment references and next additions

Research checked 2026-10-04. The comparison below focuses on equipment that
changes play, alongside the firearm categories. The proposed additions are
design choices for Gold Element; they are not claims that these systems already
exist in the game. Manufacturer capabilities below describe their products,
not independently verified effectiveness.

## Game references

| Reference | Equipment to draw from | Useful design lesson |
| --- | --- | --- |
| [SWAT 4 publisher manual](https://sierrachest.com/gfx/games/SWAT4/box/SWT4_Mn_TX_7162010.pdf) | Carbines, SMGs, shotguns and pistols; pepper-ball gun, less-lethal shotgun and stun gun; flashbang, CS and sting grenades; pepper spray, optiwand, toolkit and door wedges; C2 charges and breaching shotgun; armor, helmet and gas mask. | Give quiet observation, quiet unlocking, loud entry and less-lethal control different costs and outcomes. |
| [SWAT: Elite Force changes](https://github.com/eezstreet/SWATEliteForce/blob/master/ALLCHANGES.md) | ARWEN 37 and HK69 launchers, additional beanbag/breaching shotguns, broader firearm/munition choices, extra helmet/armor options, grenade/wedge packs, throwable lightsticks and shared equipment. | Weight and bulk affect movement and interactions. Ammunition choice changes penetration, and less-lethal impact can still injure. |
| [SEF: First Responders](https://github.com/beppegoodoldrebel/SEF_FR) and [First Responders Tactical Edition](https://github.com/FenixP2344/FRTE-Source) | Related overhauls with new weapon/equipment models, handling, sounds and AI changes; exact catalogs vary by release. | These are likely alternatives if the remembered big mod was not Elite Force itself. Their handling/presentation work matters alongside the quantity of equipment. |
| [Ready or Not loadout overview](https://voidinteractive.net/ready-or-not/loadout/) and [tactical catalog](https://readyornot.wiki.gg/wiki/Tacticals) | Firearm platforms and attachments; beanbag/pepper-ball weapons; taser and pepper spray; C2, wedges and lockpick gun; optical wand, shields, ram, breaching shotgun and flash/CS/stinger launchers. | Distinguish small tactical devices from a limited long-tactical slot. Shields, observation tools and launchers compete for that slot. |

Elite Force is the most likely match for the remembered SWAT 4 overhaul. Its
[project description](https://github.com/eezstreet/SWATEliteForce) identifies
additional equipment, weight/bulk, equipment sharing, trap interactions and
expanded ammunition. First Responders and Tactical Edition are related projects,
not interchangeable names for the same release.

## Current slice

Gold Element now has reusable lockpicks and finite door-mounted breaching
charges, in addition to the existing impact launcher, taser, optiwand, ram,
flashbangs, CS and gas masks. Picking requires a held interaction and leaves
the leaf closed. Charges have a mount hold and a separate owner-controlled
remote; the world, damage, inventory, sounds and co-op replicas share their
state. The blast is a simple visibility/distance game effect. Physical debris,
pressure simulation and explosive formulation are outside this implementation.

The sniper camera also supplies a reusable presentation pattern for future
remote cameras: a live inset, source selection, close and floating takeover.

## Less-lethal additions

| Candidate and source | Documented capability | Proposed Gold Element behavior |
| --- | --- | --- |
| [PepperBall VKS PRO](https://pepperball.com/blog/pepperball-unveils-the-new-vks-pro-as-a-powerful-non-lethal-option-for-law-enforcement/) | A semi-automatic, multi-payload launcher introduced in 2023, with magazine/hopper feed options. | Physical breakable projectiles, local irritant exposure, limited ammunition, mask resistance and visible impacts. This adds sustained ranged control to the current single-impact weapon. |
| [Axon TASER 10](https://www.axon.com/products/taser-10) | Up to ten independently targeted probes, a stated maximum range of 45 feet, and an audible/visual warning feature. | A separate advanced profile with individual darts, two-contact connection checks, visible tethers and warning-driven compliance. Keep the current simple taser as its own clearly named profile until those mechanics exist. |
| [Defense Technology 40 mm impact/payload rounds](https://www.defense-technology.com/wp-content/uploads/2020/06/DT_40mm_Exact_iMpact_Direct_Impact_Sell_Sheet.pdf) | Dedicated impact rounds and impact rounds carrying a payload. | A launcher with meaningful ammunition selection: sponge impact, CS, flash or other explicitly defined game effects. Physical travel, reloads and body-region injury should distinguish it from the current traced impact gun. |
| [BolaWrap 150 manufacturer manual](https://wrap.com/wp-content/uploads/2024/01/BW_PROD_Manual_01-23-2024_v1.pdf) | A remote restraint device. | An experimental tether restraint that checks geometry and restricts movement, followed by normal cuffing. It needs movement/animation support before it can be a reliable gameplay option. |

Pepper spray is the smaller immediate addition: a short, occluded cone with
finite stock, gas-mask resistance and a temporary compliance opportunity. Door
wedges belong beside it: visible physical placement, persistent denial of a
door and deliberate removal. Both need useful suspect movement and door use to
reach their full value.

## Reconnaissance and communication gadgets

| Candidate and source | Documented capability | Proposed Gold Element behavior |
| --- | --- | --- |
| [Bounce Imaging throwable cameras](https://bounceimaging.com/tactical/) | Stabilized panoramic video from a camera that can be thrown or used on a pole. | Throw a physical camera, then select its live feed in the inset. Recover it to reuse it; cover and orientation still matter. |
| [Pit Viper 360 announcement](https://bounceimaging.com/worlds-first-thermal-360-throwable-camera-revealed-at-ntoa/) | A thermal panoramic throwable camera announced in 2024. | A later thermal feed with material-aware visibility. Thermal vision needs its own visibility model; it should not reveal actors through opaque walls. |
| [ReconRobotics Throwbot 2](https://reconrobotics.com/products/throwbot-2-robot/) | A throwable, remotely driven micro-robot carrying video/audio reconnaissance. | Slow remote movement through the same collisions, a controllable camera, listening and physical vulnerability. A larger implementation than a stationary throwable camera. |
| [BRINC product FAQs](https://brincdrones.com/faqs/) | BRINC Ball provides two-way communication; Lemur 2 is an indoor tactical drone platform. | Start with a throwable communication device and negotiation/compliance commands. A piloted drone additionally needs flight, collision, perception and exposure rules. |

## Suggested order

1. Door wedges and pepper spray, with suspect reactions to doors and noise.
2. A physical PepperBall profile and selectable less-lethal launcher rounds.
3. A throwable camera using the live camera panel and existing canister physics.
4. Individual-probe/tether taser mechanics and stronger compliance variation.
5. A shield, ground robot and communication gadget after body poses, NPC
   movement and squad commands can support their distinct roles.

Evidence collection, an explicit force-use/debrief record and more varied
suspect/civilian behavior should grow alongside this list. Equipment needs
encounters where its tradeoffs are visible, measurable and worth choosing.

## Implemented encounter additions

Wedges and pepper spray now use finite inventories and authority checks. Wedges
require a held placement/removal and persist on the shared hinged leaf; spray
checks a short occluded cone and gas-mask resistance. Unlocked door peeking and
optical trap discovery/disarming are also implemented. Physical PepperBall,
launcher payload selection, remote cameras/robots/drone, individual taser
probes, shield and communication-device mechanics are the next equipment work.

Physical PepperBall/impact/CS/flash launcher profiles, a physical ten-probe CEW
with two-contact/clear-tether connection, and an experimental temporary tether
restraint are now implemented. Throwable cameras, slow robots and drones, and
an audio-only communication ball share the live feed interface with A/B. They
have finite inventory, collision, owner controls, recovery, batteries and damage.
These are fictional gameplay models; thermal imaging, shield mechanics, richer
animations and production asset/audio integration remain further work.

## Weapon data files

The twelve weapon profiles live in the compiled `swat_weapon_defs` table in
`weapons.c`. That table is the default, and it stays the authority until a
file is loaded on purpose. The player, the dedicated server and
`swat_replay_tool` take `--weapon-data FILE.ini`. They load the file at
startup, before any simulation exists. The training adapter never loads one.

A file is an overlay on the compiled table. Keys you leave out keep their
compiled values:

```
# '#' or ';' starts a comment.
version = 1

[profile 7]
name = GE COMPACT MK2
capacity = 20
automatic = 0

[profile 1]
reload_ticks = 60
```

The keys are the `SwatWeaponDef` field names. Each value must fall in its range:

| Key | Range |
| --- | --- |
| `name` | 1 to 31 printable ASCII characters, without `#` or `;` |
| `capacity` | 1 to 100 |
| `shot_ticks`, `equip_ticks` | 1 to 600 |
| `reload_ticks`, `empty_reload_ticks` | 1 to 3600 |
| `damage` | 0 to 1000 |
| `range` | 0.1 to 500 m |
| `energy` | 0.001 to 10 |
| `hip_spread`, `ads_spread` | 0 to 30 degrees |
| `recoil` | 0 to 20 |
| `automatic` | 0 or 1 |
| `barrel` | 0.1 to 1.5 m |
| `sight_height` | 0 to 0.3 m |

Profile 0's `barrel` and `sight_height` must keep the measured carbine values
from `rifle_geometry.h`. Those values also drive clearance and ADS; see
[WEAPON_ART.md](WEAPON_ART.md). The tick ranges keep the observation's
divisors positive and stay inside the snapshot decoder's limits. A file sets
numbers only. Profile indices still choose the behavior that is keyed to them:
launcher payloads (2, 5, 8, 9), breach rounds (6), the probe launcher's empty
reserve (10) and the sniper rifles (3, 4).

Loading is all or nothing. Any of the following rejects the whole file and
leaves the active table as it was:

- a missing or unsupported `version`
- an unknown or repeated section or key
- a key outside a `[profile N]` section
- a malformed or non-finite number
- a value outside its range
- a line longer than 254 characters

The log names `path:line` and the reason. `swat_weapons_save()` writes the
active table in this format, and float32 values round-trip exactly.

### Determinism

`swat_weapons_table_id()` is 0 when the active table equals the compiled one,
whether or not a file was loaded. Otherwise it is an FNV-1a digest of every
field with the top bit set. Replays and the network check this id the way
they already check config:

- A journal recorded with the compiled table keeps the version 1 header,
  byte for byte. A journal recorded with a loaded table is version 2, which
  adds the id after the flags word. A journal opens only when the running
  table has the same id; otherwise F9 and the replay tool report a weapon
  data mismatch.
- ENet connect data is magic + `SWAT_NET_VERSION` + id. Default peers send
  the value they always have. A peer with a different table is refused with
  "Game network versions or weapon data do not match."

Neither change needed a version bump. `SWAT_NET_VERSION` stays 49, and
existing journals still load. Peers built before this change refuse peers
running a loaded table, because the connect values differ.

### Reload events

The staged reload (REMOVE until `duration/4`, INSERT until `duration*2/3`,
then CHAMBER) is reported as `SwatWeaponEvent` bits on each `SwatWeapon`
after every `swat_weapons_step`:

- start
- magazine out
- magazine in
- done
- round chambered
- cancelled, from `cancel_reload`
- switched, from a weapon change during a reload

Interrupts are reported only when a reload was running. The bits do not
drive the stage machine. They are not replicated, and they are not part of
the replay digest. `swat_reload_stage_end()` returns the shared stage
boundaries. `swat_character_reload_time()` in `character_runtime.c` still
repeats them inline. `make weapons-test` checks these events against a
frozen copy of the earlier step.
