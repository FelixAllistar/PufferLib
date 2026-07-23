# Abyss simulation specification

This records sourced mechanics and user-observed assumptions separately. Uncertain values
remain configurable instead of becoming hidden constants.

## Filaments and weather

One weather roll applies to players and NPCs for all three rooms in an Abyss.

| Weather | Penalty | Bonus |
|---|---|---|
| Dark | -30/50/70% turret optimal and falloff | +50% maximum velocity |
| Electrical | -30/50/70% EM resistance | -50% capacitor recharge time |
| Exotic | -30/50/70% kinetic resistance | +50% scan resolution |
| Firestorm | -30/50/70% thermal resistance | +50% armor HP |
| Gamma | -30/50/70% explosive resistance | +50% shield HP |

Tiers 0-3 roll 30% or 50%; tiers 4-6 roll 50% or 70%. The current T0 prior heavily
favors 30%, based on user observation. Dark boosts NPC chase speed, but not their velocity
at the desired orbit. Source: <https://wiki.eveuniversity.org/Abyssal_Deadspace>

## Local clouds

Clouds are irregular volumes. The runtime uses a union of two to four overlapping oriented
ellipsoids. Their distributions still need calibration against visual/live captures.

| Cloud | Effect |
|---|---|
| Filament/orange | -40% shield boost amount and -40% duration; conventional booster cap/s effectively +66% |
| Bioluminescence/light blue | signature radius x4 |
| Tachyon/white | maximum velocity x4 and inertia modifier x0.5, producing x8 acceleration |

Source: <https://wiki.eveuniversity.org/Abyssal_Deadspace#Localized_Effects>

## Support pylons

| Type ID | Structure | Range | Effect |
|---:|---|---:|---|
| 47437 | Short Deviant Automata Suppressor | 15 km | 2 s cycle; 30 thermal + 30 explosive |
| 47438 | Medium Deviant Automata Suppressor | 40 km | 2 s cycle; 10 thermal + 10 explosive |
| 47469 | Short Multibody Tracking Pylon | 15 km | +80% tracking; raw type-page verification pending |
| 47470 | Medium Multibody Tracking Pylon | 40 km | +60% tracking |

The available type data reports a 7.5 km structure radius. Suppressor damage applies only
to missiles/rockets, player drones, Vila Swarmers and other eligible rogue-drone auxiliaries,
and `*-lance Tessella` drone frigates. It never damages player ships or ordinary NPC ships.

Eligibility and progression are independent:

- Vila Swarmers are suppressor-vulnerable but do not count toward gate unlock.
- `*-lance Tessella` are suppressor-vulnerable and gate-required.
- Ordinary required NPCs are gate-required but suppressor-immune.

Sources: <https://everef.net/types/47437>, <https://everef.net/types/47438>,
<https://everef.net/types/47469>, and <https://everef.net/types/47470>.

## Current player fit

- 735 shield HP, 1080 armor HP, 920 hull HP
- shield resists 0/20/40/50%; armor 50/35/25/20%; hull 33/33/33/33%
- 717 GJ capacitor, 227 s recharge attribute
- 37.2 m signature, 578 mm scan resolution, 33 km lock range, 5 targets
- 306 m/s unpropelled and 1546 m/s with the current MWD fit
- 244 DPS, 537 volley, 15.9 km optimal, 2.88 km falloff, 276 tracking
- Small ACM Compact Armor Repairer: 79 armor HP, 40 GJ, 4.8 s cycle

## Policy no-op semantics

Every action lane has HOLD/NOOP. The policy can hold all lanes or independently change
navigation, focus, weapon, propulsion, repair, or interaction. This permits waiting under
entry invulnerability while eligible NPCs are killed by a suppressor.
