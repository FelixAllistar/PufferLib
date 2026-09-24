# Resolved ship profile

The Abyss simulator consumes final fitted attributes from `config/abyss.ini`.
It does not apply hull, skill, implant, rig, ammunition, or stacking bonuses.
Resolve those bonuses in EVE or a fitting tool, then copy the resulting values
into the `[env]` profile block.

## Required hull values

- Shield, armor, and hull hit points, plus the displayed passive shield
  recharge time in seconds.
- EM, thermal, kinetic, and explosive resistance for each hit-point layer.
- Capacitor capacity and the displayed capacitor recharge time in seconds.
- Base mass, propulsion-module mass addition, inertia modifier, base speed,
  propulsion speed, base signature radius, and propulsion signature multiplier.
  A displayed `+500%` MWD signature modifier is a `6.0` total multiplier: this
  profile's 62 m base radius therefore becomes 372 m while the MWD cycle is active.
- Scan resolution and maximum targeting range.

Shield and capacitor recharge times are the displayed full recharge attributes,
not `capacity / passive HP/s` or `capacity / passive GJ/s`. The simulator
applies EVE's nonlinear recharge curve to both pools.

## Required module values

The current policy controls one grouped weapon, one propulsion module, and one
local repair module. Their simulator representation is cycle based:

- Activation capacitor is removed when a cycle starts.
- A grouped weapon's activation cost is `weapon_count * weapon_cap_cost_each`.
- Shield boosts use `rep_layer = 0` and `rep_effect_timing = 0`, applying at the
  start of a paid cycle.
- Armor repairers use `rep_layer = 1` and `rep_effect_timing = 1`, applying when
  the paid cycle completes even if auto-repeat was turned off meanwhile.
- Propulsion mass, speed, and signature changes apply while propulsion is on.

For another ship, copy the resolved profile block rather than changing simulator
code. Future multiple-repair, resistance-module, and drone controls should add
module instances while retaining these same activation and effect semantics.
