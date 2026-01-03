'''Great Olm boss fight from Old School RuneScape as an RL environment.'''

import gymnasium
import numpy as np

import pufferlib
from pufferlib.ocean.raid import binding

# Observation size per player (must match OBS_SIZE in raid.h)
OBS_SIZE = 26

# Action space size (must match NUM_ACTIONS in raid.h)
NUM_ACTIONS = 31


class Raid(pufferlib.PufferEnv):
    def __init__(self, num_envs=1, num_players=1,
                 arena_width=23, arena_height=18,
                 max_episode_ticks=10000,
                 player_damage=30, player_hit_chance=70,
                 olm_base_damage=20, prayer_reduction=80,
                 render_mode=None, log_interval=128, buf=None, seed=0):
        '''Initialize the Great Olm boss fight environment.

        Args:
            num_envs: Number of parallel environments
            num_players: Number of players per environment (1-N)
            arena_width: Width of the arena in tiles (default 23)
            arena_height: Height of the arena in tiles (default 18)
            max_episode_ticks: Maximum ticks before episode timeout
            player_damage: Damage per successful player hit
            player_hit_chance: Hit chance percentage (0-100)
            olm_base_damage: Base damage per Olm attack
            prayer_reduction: Damage reduction % with correct prayer
            render_mode: Rendering mode (None or 'human')
            log_interval: Steps between log aggregation
            buf: Optional buffer for observations/actions
            seed: Random seed
        '''
        self.single_observation_space = gymnasium.spaces.Box(
            low=-1, high=1, shape=(OBS_SIZE,), dtype=np.float32)
        self.single_action_space = gymnasium.spaces.Discrete(NUM_ACTIONS)

        self.render_mode = render_mode
        self.num_agents = num_envs * num_players
        self.log_interval = log_interval
        self._num_players = num_players

        super().__init__(buf)

        c_envs = []
        for i in range(num_envs):
            c_env = binding.env_init(
                self.observations[i * num_players:(i + 1) * num_players],
                self.actions[i * num_players:(i + 1) * num_players],
                self.rewards[i * num_players:(i + 1) * num_players],
                self.terminals[i * num_players:(i + 1) * num_players],
                self.truncations[i * num_players:(i + 1) * num_players],
                seed,
                arena_width=arena_width,
                arena_height=arena_height,
                num_players=num_players,
                max_episode_ticks=max_episode_ticks,
                player_damage=player_damage,
                player_hit_chance=player_hit_chance,
                olm_base_damage=olm_base_damage,
                prayer_reduction=prayer_reduction)
            c_envs.append(c_env)

        self.c_envs = binding.vectorize(*c_envs)

    def reset(self, seed=0):
        binding.vec_reset(self.c_envs, seed)
        self.tick = 0
        return self.observations, []

    def step(self, actions):
        self.tick += 1
        self.actions[:] = actions
        binding.vec_step(self.c_envs)

        info = []
        if self.tick % self.log_interval == 0:
            log = binding.vec_log(self.c_envs)
            if log:
                info.append(log)

        return (self.observations, self.rewards,
                self.terminals, self.truncations, info)

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)


class Policy(pufferlib.models.Default):
    '''Default policy for the Raid environment.'''
    def __init__(self, env, hidden_size=128, **kwargs):
        super().__init__(env, hidden_size=hidden_size, **kwargs)


if __name__ == '__main__':
    # Benchmark performance
    import time

    N_ENVS = 512
    N_PLAYERS = 1

    env = Raid(num_envs=N_ENVS, num_players=N_PLAYERS)
    env.reset()
    steps = 0

    CACHE = 1024
    actions = np.random.randint(0, NUM_ACTIONS, size=(CACHE, N_ENVS * N_PLAYERS))

    i = 0
    start = time.time()
    while time.time() - start < 10:
        env.step(actions[i % CACHE])
        steps += env.num_agents
        i += 1

    elapsed = time.time() - start
    sps = int(steps / elapsed)
    print(f'Raid SPS: {sps:,} ({N_ENVS} envs x {N_PLAYERS} players)')

    env.close()
