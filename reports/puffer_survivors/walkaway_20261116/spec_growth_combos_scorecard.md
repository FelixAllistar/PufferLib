# Spec: capped growth, three combos, fun scorecard (Puffer Survivors)

Scope: only `ocean/puffer_survivors/**` and `config/puffer_survivors.ini`. No trainer-core (`src/`) edits.
Do not commit. Do not build `./puffer` (other sessions use it); the trainer builds as
`bash build.sh puffer_survivors puffer_ps --cu` from the repo root.

The sim is one header, `ps_sim.h`, compiled twice: CPU (state in the `Env` struct, `ps_state.h`) and
CUDA (structure-of-arrays in `cuda/ps_cuda_sim.cu`, one `PS_BLOB_FIELD` per array). Every new piece of
state needs both. Access goes through the existing macros (`PS_P` per-env scalar, `PS_W(sim, env, i,
field)` small per-env arrays such as `weapon_level`, `PS_ENEMY`, `PS_AREA`). Every new config value needs:
a `PSConfig` field (`ps_constants.h`), a line in `ps_config_from_kwargs` (`puffer_survivors.h`), and a key
in `config/puffer_survivors.ini` (a missing key fails fast). No recursion anywhere (CUDA kernels).

## A. Passive upgrades are capped

- `#define PS_PASSIVE_COUNT (PS_UPGRADE_COUNT - PS_WEAPON_COUNT)` (= 7).
- State `int passive_level[PS_PASSIVE_COUNT]`, zeroed in `ps_reset_core` before `free_upgrade` is applied.
- `ps_apply_upgrade_effect`: for a passive `u`, `passive_level[u - PS_WEAPON_COUNT]++` (free upgrades too).
- Config `upgrade_passive_max_level = 5`. `ps_upgrade_available(passive)` is true iff its level is below it.
- `ps_offer_upgrades`: let A = number of available upgrade types.
  - A == 0: no card is offered. `hp = min(max_hp, hp + health_heal)`, `queued_upgrades = 0`,
    `pending_upgrade` stays 0, return. (Each level-up then heals instead.)
  - Slots `i < A`: distinct available types, sampled as today. Slots `i >= A`: `offered[i] = offered[i - A]`.
  The current rejection loop never terminates when fewer than 3 types are available; this fixes that.

## B. Sizes (config, plus one formula)

- Spikes shot count: today `8 * level`. New: `spikes_count_base + spikes_count_per_level * (level - 1)`,
  config `spikes_count_base = 8`, `spikes_count_per_level = 4` (8 at level 1, 36 at level 8).
- Config values: `upgrade_area_bonus` 0.12 -> 0.06; `weapon_sonar_radius_per_level` 0.52 -> 0.30;
  `weapon_whirlpool_radius_per_level` 0.24 -> 0.15; `weapon_ink_radius_per_level` 0.18 -> 0.10;
  `weapon_orbit_distance_per_level` 0.18 -> 0.10.
- `PS_AREA_STORAGE_CAP` 96 -> 160 and ini `area_cap` 96 -> 160 (room for the combo areas below).
- README: the Spikes sentence ("8 shots at level 1 to 64 at level 8") must match.

## C. Combos

Two new area types after the weapons: `PS_AREA_DETONATION = PS_WEAPON_COUNT`, `PS_AREA_SHATTER =
PS_WEAPON_COUNT + 1`. `ps_render.h` indexes visuals by area type (around line 523): draw DETONATION with
the Sonar look and SHATTER with the Glacier/whirlpool burst look; nothing may index out of range.

Add `ps_spawn_area_delayed(..., int first_tick_delay)`: same as `ps_spawn_area` but `tick_timer =
first_tick_delay`. `ps_update_areas` decrements `tick_timer` and then fires when it is `<= 0`, so a delay
of d fires d ticks later. `ps_spawn_area` becomes the d = 0 case.

New per-env transient `int damage_source` (-1 normally). `ps_update_areas` sets it to the area's type
around its `ps_damage_radius_with_query_pad` call and back to -1 after.

New per-enemy `float pulled` (like `slow`): ticks left of the "pulled" status; reset on spawn, decremented
toward 0 in `ps_update_enemies` next to `slow`.

1. **Whirlpool pulls.** Config `whirlpool_pull = 0.9`, `whirlpool_pull_radius_scale = 2.0`,
   `whirlpool_pull_ttl = 45`. In `ps_cast_whirlpool`, after the existing damage call (radius R): scan
   active enemies (same scan pattern as `ps_cast_glacier`); for circle-shaped enemies without the BOSS or
   ARI_K flag at distance d from the player with `R < d <= R * whirlpool_pull_radius_scale`: move it along
   the line to the player so its new distance is `max(0.9 * R, d - whirlpool_pull)`, and set
   `pulled = whirlpool_pull_ttl`. Enemies inside R keep today's knockback.
2. **Sonar detonates oil.** Config `oil_detonate_radius_scale = 1.6`, `oil_detonate_damage_scale = 4.0`,
   `oil_detonate_delay = 8`, `oil_detonate_ttl = 10`. In `ps_cast_sonar`, after its own damage (radius S):
   every active area of type `PS_WEAPON_INK` (pools and trail) whose centre is within `S + its radius` of
   the player becomes a detonation in place: `type = PS_AREA_DETONATION`, `radius *= radius_scale`,
   `damage *= damage_scale`, `tick_rate = 9999`, `tick_timer = 1 + (int)(oil_detonate_delay * min(1,
   dist / S))`, `ttl = tick_timer + oil_detonate_ttl`, and `active_ink_count--` (it is no longer oil;
   check that `ps_deactivate_area` only decrements that count for INK-typed areas).
3. **Freeze and shatter.** Glacier already sets `slow` on survivors and `ps_update_enemies` multiplies
   speed by `1 - frost_slow` while `slow > 0`.
   - Config `frost_slow` 0.2 -> 1.0 (a full stop), `frost_slow_ttl` 60 -> 90, `frost_range` 8.0 -> 10.0,
     `frost_half_angle` 0.7 -> 0.9, new `frost_boss_slow = 0.3`: enemies with the BOSS or ARI_K flag use
     `frost_boss_slow` in place of `frost_slow`.
   - Config `shatter_damage = 1.5`, `shatter_radius = 1.6`, `shatter_delay = 4`, `shatter_ttl = 8`.
     In `ps_damage_enemy`, when an enemy dies with `slow > 0` and `shatter_damage > 0`:
     `ps_spawn_area_delayed(PS_AREA_SHATTER, enemy x, enemy y, shatter_radius, shatter_damage * (1 +
     damage_bonus), shatter_delay + shatter_ttl, 9999, shatter_delay)`. A shatter that kills another frozen
     enemy spawns the next one (a chain, 4 ticks per link, bounded by the area cap).

## D. Fun scorecard (per episode, in the sim, logged)

New per-episode state (reset in `ps_reset_core` with the other `episode_*` fields):

- `episode_combo_kills`: in `ps_damage_enemy` at a kill, +1 if `slow > 0` or `pulled > 0` on that enemy, or
  `damage_source` is `PS_AREA_DETONATION` or `PS_AREA_SHATTER`.
- Burst kills: `window_kills` counts kills; at every tick where `tick % 30 == 0`: if `window_kills >=
  fun_burst_kills` then `episode_burst_kills += window_kills`; then `window_kills = 0`. Config
  `fun_burst_kills = 10`.
- Coverage: each tick `c = min(1, (sum over active areas of PI * r^2 + orbit_count * PI * orbit_hit_r^2)
  / (fun_view_width * fun_view_height))` where the orbit terms are those `ps_cast_orbit` computes (0 when
  Orbit is level 0). `episode_coverage_sum += c`. Config `fun_view_width`, `fun_view_height`: the world
  units the play window shows; take the real values from the renderer's camera and say where they came
  from in the ini comment.
- `episode_min_hp_frac`: running minimum of `hp / max_hp` (starts at 1).
- Kill growth: `minute_kills` counts kills; at every tick where `tick % 3600 == 0`: if this is the first
  such tick `first_minute_kills = minute_kills`; `last_minute_kills = minute_kills`; `minute_kills = 0`.
- Straightness: window of 600 ticks. Track window start position and path length walked in the window.
  At every tick where `tick % 600 == 0`: if path > 1, `straight_sum += net displacement / path`,
  `straight_n++`; restart the window.

Band score: `band(x, lo, hi, soft) = 1` if `lo <= x <= hi`, else `exp(-(d/soft)^2)` with `d` the distance
from x to the band. In `ps_add_log`, with `T = tick`, `K = max(1, episode_kills)`:

| Term | x | lo | hi | soft |
|---|---|---|---|---|
| tension | `episode_min_hp_frac` (0 if dead) | 0.15 | 0.50 | 0.30 |
| damage | `episode_damage_taken / (T / 3600)` | 1.0 | 4.0 | 1.5 |
| survival | `T / max_steps` | 0.85 | 1.0 | 0.30 |
| coverage | `episode_coverage_sum / T` | 0.03 | 0.25 | 0.10 |
| combo | `episode_combo_kills / K` | 0.30 | 0.70 | 0.20 |
| burst | `episode_burst_kills / K` | 0.20 | 0.70 | 0.20 |
| growth | `log2(max(1, last_minute_kills) / max(1, first_minute_kills))` | 3.0 | 5.3 | 1.5 |
| straight | `straight_sum / max(1, straight_n)` | 0.15 | 0.50 | 0.20 |

All 24 band numbers are config: `fun_<term>_lo`, `fun_<term>_hi`, `fun_<term>_soft`. `fun` = product of
the eight scores. `Log` gets `fun`, the eight scores as `fun_<term>`, and the eight raw x values as
`raw_<term>`; `puf_log` exports them all (they are summed per episode and divided by `n` like the rest).

Probe (`tests/probe_policy.c`): after the per-level table print a "scorecard" block from `env.log`
(means over the episodes it played: each raw value, each score, `fun`). Do not remove existing output.

## Tests (`tests/test_cpu.c`, and `tests/test_cuda.cu` where it already mirrors a CPU check)

Hand-checkable cases, each asserting exact numbers:

1. Passive cap: five Speed picks -> Speed unavailable; with everything capped a level-up offers nothing,
   heals `health_heal`, leaves `pending_upgrade == 0`; with exactly 2 types available `offered[2] == offered[0]`.
2. Spikes count at levels 1 and 8 (8 and 36).
3. Whirlpool pull: an enemy placed at 1.5 R on the +x axis ends at `max(0.9 R, 1.5 R - 0.9)` and has
   `pulled == whirlpool_pull_ttl`; a boss at the same place does not move.
4. Detonation: one oil area inside Sonar range becomes DETONATION with radius x1.6 and damage x4,
   `active_ink_count` drops by one, and an enemy inside it takes exactly that damage once.
5. Freeze: a frozen normal enemy does not move for a tick; a frozen boss moves at 0.7 of its speed.
6. Shatter chain: three frozen enemies in a line 1.0 apart with hp below the shatter damage; killing the
   first kills the second 4 ticks later and the third 8 ticks later; `episode_combo_kills == 3`.
7. `band()` at lo, hi, and one `soft` outside (expect 1, 1, exp(-1)).

Run and report the output of: `make -C ocean/puffer_survivors test sanitize`, `make -C
ocean/puffer_survivors NVCC=/usr/local/cuda-12.6/bin/nvcc cuda-test`, `make -C ocean/puffer_survivors
probe viewer`, and the trainer build above. Report anything in this spec that did not fit the code as
written instead of working around it silently.
