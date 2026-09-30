# New ship → T0 Electrical → live parity

Reviewed 2026-09-28 against local source. Subsequent screenshots are captured
in `PUNISHER_ELECTRICAL_INTAKE.md`; the Xray-only Punisher profile is now applied
to `config/abyss.ini`. No Punisher policy has been trained or executed live yet.

Later update: the user trained and ran the Punisher policy successfully. Hunter
review exposed dropped NPC repair metadata and clipped speed rewards; both are
now addressed. See `SWEEP_PUNISHER.md` for the repaired model, current sweep
configuration, baseline evaluation and remaining approximation details. The
earlier status statements below describe the initial intake.

## Where things live

- Sim repository: `/home/felix/puffertank/pufferlib` (run build/eval commands here).
- Mechanics: `ocean/abyss/abyss.h`; fitted attributes: `config/abyss.ini`.
- Fit field guide: `ocean/abyss/SHIP_PROFILE.md`.
- Bot: `C:\Users\sunde\projects\Sanderling\recorder\src\Sanderling.Controller`.
- Recorder reads native motion + asynchronous UI, fuses canonical entities and
  records observations/actions to Sanderling `runs/<session>/events.jsonl`.
- `Sim2Real/AbyssPpoPlanner.cs` uses native PPO inside authoritative active rooms;
  rules handle setup/transitions. The semantic executor revalidates identity and
  fresh geometry, reconciles module cycles, and serializes pointer actions.
- `AbyssNativeInferenceClient.cs` loads native FP32 MinGRU `.bin` weights directly.
  Pin a checkpoint path: `latest` checks shape/architecture, not fit or weather.

## Actual contracts and simulation scope

Native policy: 1,224 floats, 64 room-stable slots, six heads
`[66,129,65,2,2,130]`, 394 mask entries. The separate schema-v4 recorder contract
has 1,059 floats and 16 slots; its fixtures are not directly native PPO inputs.

The sim advances at one second; the bot normally infers every 900 ms and also
reconsiders executor settlements. Recurrent state spans all three rooms; entity
slots reset per room. Persistent targeted-fire/open/activate transactions matter.

Only the 28 recorded T0 three-room templates are sampled. QSNA T1 data exists
but is not wired into runtime; changing `filament_tier` does not add T1 spawns.
The existing handoff identifies the captures as Dark and missing the rare Vila
swarm. Electrical reuses those layouts, not an independently validated corpus.

## Bot profile update (completed after screenshot intake)

Sanderling now bundles an identical copy of
`data/profiles/punisher_t0_electrical_xray.ini`. Normal CLI sessions load it by
default; `--ship-profile PATH` accepts another simulator INI. The adapter reads
propulsion speed and lock range, encodes configured weather type correctly, and
no longer derives weather from gun range. Both rules and PPO use the same lock
range. Filament name follows profile tier/weather; armor repair remains required.
Session metadata logs the profile path/hash and effective values.

The weather penalty is still an explicit assumption: default .50, override
`--weather-penalty 0.30` for a known weaker roll. No automatic live effect-strength
sensing was added. Default HUD keys remain group 1 / propulsion Alt+1 / repair
Ctrl+1. The bot was rebuilt in Release and all 317 controller tests passed,
including profile validation, Electrical/Dark encoding and configurable lock
boundaries. No live input/attachment was used for this change.

## Original audit findings and remaining gaps

Items 1–2 below are resolved by the profile update above; their descriptions
record the original bugs. Item 3's filament default is now Electrical; module
mapping and live telemetry still require checking on the actual HUD.

1. `Sim2Real/AbyssNativeObservation.cs` hard-codes `PropSpeed=1546`, compares
   live weapon optimal/max range to `15900`, and writes Dark weather (`o[18]=0`,
   `o[17]=.75`). Electrical must encode weather type `1/4`, range multiplier `1`,
   velocity multiplier `1/2`, and its actual penalty. Weapon range cannot reveal
   Electrical's EM penalty. Share explicit fit/weather values with the sim;
   do not infer weather from a different gun's range or EWAR-modified optimal.
2. `AbyssNativeActionContract.LockRangeMeters=33000` drives the PPO lock mask,
   independently of the configurable rules `--lock-range` (default 50000).
3. `ControllerConfig` defaults to `Tranquil Dark Filament`, armor repair logic,
   weapon `1`, propulsion `Alt+1`, repair `Ctrl+1`. Verify new module roles/keys
   and rules fallback behavior, especially if the new ship shield-tanks.
4. Live native encoding substitutes zero for missing HP/cap/timer, zero cloud
   occupancy, initial HP for unseen target layers, and a fallback NPC identity.
   Check actual sensor evidence; coherent frames alone do not prove all fields.
5. NPC motion, cloud geometry and collision/latency behavior need replay drift
   measurements. Native observation parity must be checked separately from
   the richer schema-v4 adapter tests.

The sim already implements Electrical EM-resistance adjustment and halves cap
recharge time. Use outside-Abyss resolved fit values to avoid applying weather
twice. Current config is now Electrical and deliberately oversamples hard scenarios
(`hard_scenario_probability=.5`) and strong weather (`weather_high_penalty_probability=1`).
The documented .10 strong-weather prior is a prior, not verified Electrical data.

## Screenshot intake and next steps

Capture fitted stats with the intended character, loaded ammo, and known module
state: hull/fit overview; all layer HP/resists; cap capacity/recharge; shield
recharge; mass/inertia/signature/speeds; targeting range/scan resolution; gun
damage/volley/cycle/optimal/falloff/tracking/cap cost; ammo damage split; propulsion
and repair attributes. Include module names, grouped weapon count and HUD keys.
Note implants, heat, boosters and active hardeners. Missing fields remain explicit
unknowns; fitting exports can identify modules but do not replace resolved stats.

1. Transcribe a source-labelled fit table; distinguish grouped volley from
   per-gun activation cost, fractions from percentages, seconds from ms, and
   displayed recharge time from peak recharge rate. Check tracking units.
2. Configure T0 (`filament_tier=0`) Electrical (`weather_type=1`), fix the live
   adapter/profile mismatches above, and add focused cross-boundary tests.
   Current controls cover one grouped turret weapon, propulsion and one repair;
   drones, missile-player mechanics, reload/ammo switching or extra active
   modules require checking/extending support if the new fit needs them.
3. Build with `bash build.sh abyss`; train with `./puffer train`. Freeze the fit,
   config, source revision and explicit checkpoint for evaluation/live comparison.
   Evaluate every template at both 30% and 50% penalty, plus random episodes;
   report deaths, timeouts, caches, cap starvation and gun application separately.
4. Start with observe-only bot validation, then the user's supervised T0 trial.
   Compare recorded motion, damage/cap/repair cycles, locks, action landing times,
   weather and room outcomes to simulation before considering T1.

`tests/eval_rules.c` remains an armor-specific heuristic, not a validated fit
oracle. During Punisher intake its stale module/weapon action semantics were
corrected to emit complete desired state every tick, and its weather selection
was moved before reset so Electrical resistance effects use the selected roll.

Verification: `make -C ocean/abyss verify` passed on 2026-09-28 (native smoke
and ASan/UBSan). This is mechanics regression coverage, not evidence of live
Electrical parity. No Windows/controller tests or live session were run here.
