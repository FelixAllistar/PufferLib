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
