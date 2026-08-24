# shenaniguns

Top-down arena gunfight gym — the training half of the Shenanigans FPS NPC
pipeline. Duel (selfplay slots) + optional scripted bots, Quake-style movement,
hitscan combat, raycast vision. Pure C, no assets, no CUDA required to start.

## Run

```bash
./build.sh shenaniguns --fast   # standalone playable demo
./shenaniguns                   # WASD/arrows move+strafe, SPACE fire, R reset
./shenaniguns --bench           # headless SPS benchmark

./build.sh shenaniguns          # native train/eval binary (puffer trainer)
```

Env kwargs: `num_agents` (<=2), `num_bots` (<=6), `width`/`height` (meters),
`max_ticks`, `reward_damage_dealt`, `reward_damage_taken`, `reward_kill`,
`reward_die`, `reward_aim`, `bot_spread_deg`, `bot_reaction_deg`.

## THE CONTRACT

This env and the box3d game share units, constants, obs layout, and action
space. Deployment maps them like this:

- **Sim**: meters, fixed dt = 1/60. Gym tick == game physics tick.
- **Movement model**: identical to `puffertank/pd64/src/movement.h`
  (`MAX_SPEED 6`, `GROUND_ACCEL 60`, `FRICTION 8`, `STOP_SPEED 2`). The gym
  integrates it directly; the game applies the same wish-dir math through
  box3d body velocities. Same inputs => same trajectories (validated via the
  determinism harness).
- **Combat**: gym hitscan == game server-authoritative hitscan w/ lag comp.
- **Bots deploy as players**: policy outputs -> discrete action indices ->
  server input struct. Nothing about the netcode cares whether inputs come
  from a human or an ONNX forward pass.

### Observations (float32[62])

| idx | meaning | scale |
|-----|---------|-------|
| 0-1 | absolute x,y | /arena size |
| 2-3 | ego-frame velocity (fwd, right) | /6 m/s |
| 4-5 | sin(yaw), cos(yaw) | [-1,1] |
| 6 | hp | /100 |
| 7 | ammo | /12 |
| 8 | reload progress | [0,1] |
| 9 | fire cooldown progress | [0,1] |
| 10-25 | 16 view rays, 120 deg FOV cone | dist/24m |
| 26-61 | up to 4 nearest visible enemies x 9 floats (rel pos/vel ego, hp, aim err, dist, visible flag, reserved) | see header |

### Actions (discrete[4])

| dim | values |
|-----|--------|
| turn | {-12,-6,0,+6,+12} deg/tick |
| forward | back/none/fwd |
| strafe | left/none/right |
| fire | no/yes |

### Rewards

Damage dealt/taken per point (default +-0.01), kill +1, death -1, aim-on-target
+0.001/tick. All coefficients are kwargs — sweep them, don't hardcode.

## Sim-gap validation (before trusting any checkpoint)

1. Train N steps in-gym.
2. Export policy; drive headless box3d duels with it (same map, same constants).
3. Compare gym eval KPIs (slot_0_score, accuracy, damage ratio) vs box3d KPIs.
4. Divergence => tighten the shared constants / add the missing observation.

Only after that passes do bots enter real matches.
