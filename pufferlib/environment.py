import numpy as np

from pufferlib.exceptions import APIUsageError
import pufferlib.spaces

ERROR = '''
Environment missing required attribute {}. The most common cause is
calling super() before you have assigned the attribute.
'''

def set_buffers(env, buf=None):
    if buf is None:
        obs_space = env.single_observation_space
        env.observations = np.zeros((env.num_agents, *obs_space.shape), dtype=obs_space.dtype)
        env.rewards = np.zeros(env.num_agents, dtype=np.float32)
        env.terminals = np.zeros(env.num_agents, dtype=bool)
        env.truncations = np.zeros(env.num_agents, dtype=bool)
        env.masks = np.ones(env.num_agents, dtype=bool)

        # TODO: Major kerfuffle on inferring action space dtype. This needs some asserts?
        atn_space = env.single_action_space
        if isinstance(env.single_action_space, pufferlib.spaces.Box):
            env.actions = np.zeros((env.num_agents, *atn_space.shape), dtype=atn_space.dtype)
        else:
            env.actions = np.zeros((env.num_agents, *atn_space.shape), dtype=np.int32)
    elif not isinstance(buf.observations, np.ndarray):
        # Hack for torch 
        env.torch_observations = buf.observations
        env.observations = buf.observations.cpu().numpy()
        env.torch_actions = buf.actions
        env.actions = buf.actions.cpu().numpy() # TODO: dtype fix
        env.torch_rewards = buf.rewards
        env.rewards = buf.rewards.cpu().numpy()
        env.torch_terminals = buf.terminals
        env.terminals = buf.terminals.cpu().numpy().astype(bool)
        env.torch_truncations = buf.truncations
        env.truncations = buf.truncations.cpu().numpy().astype(bool)
        env.torch_masks = buf.masks
        env.masks = buf.masks.cpu().numpy()
    else:
        env.observations = buf.observations
        env.rewards = buf.rewards
        env.terminals = buf.terminals
        env.truncations = buf.truncations
        env.masks = buf.masks
        env.actions = buf.actions

class PufferEnv:
    def __init__(self, buf=None):
        if not hasattr(self, 'single_observation_space'):
            raise APIUsageError(ERROR.format('single_observation_space'))
        if not hasattr(self, 'single_action_space'):
            raise APIUsageError(ERROR.format('single_action_space'))
        if not hasattr(self, 'num_agents'):
            raise APIUsageError(ERROR.format('num_agents'))

        if hasattr(self, 'observation_space'):
            raise APIUsageError('PufferEnvs must define single_observation_space, not observation_space')
        if hasattr(self, 'action_space'):
            raise APIUsageError('PufferEnvs must define single_action_space, not action_space')
        if not isinstance(self.single_observation_space, pufferlib.spaces.Box):
            raise APIUsageError('Native observation_space must be a Box')
        if (not isinstance(self.single_action_space, pufferlib.spaces.Discrete)
                and not isinstance(self.single_action_space, pufferlib.spaces.MultiDiscrete)
                and not isinstance(self.single_action_space, pufferlib.spaces.Box)):
            raise APIUsageError('Native action_space must be a Discrete, MultiDiscrete, or Box')

        set_buffers(self, buf)

        self.action_space = pufferlib.spaces.joint_space(self.single_action_space, self.num_agents)
        self.observation_space = pufferlib.spaces.joint_space(self.single_observation_space, self.num_agents)
        self.agent_ids = np.arange(self.num_agents)

    def copy_data(self):
        if not hasattr(self, 'torch_observations'):
            return

        import torch
        self.torch_observations.copy_(torch.from_numpy(self.observations), non_blocking=True)
        self.torch_actions.copy_(torch.from_numpy(self.actions), non_blocking=True)
        self.torch_rewards.copy_(torch.from_numpy(self.rewards), non_blocking=True)
        self.torch_terminals.copy_(torch.from_numpy(self.terminals), non_blocking=True)
        self.torch_truncations.copy_(torch.from_numpy(self.truncations), non_blocking=True)
        self.torch_masks.copy_(torch.from_numpy(self.masks), non_blocking=True)

    @property
    def emulated(self):
        '''Native envs do not use emulation'''
        return False

    @property
    def done(self):
        '''Native envs handle resets internally'''
        return False

    @property
    def driver_env(self):
        '''For compatibility with Multiprocessing'''
        return self

    def reset(self, seed=None):
        raise NotImplementedError

    def step(self, actions):
        raise NotImplementedError

    def close(self):
        raise NotImplementedError

    def async_reset(self, seed=None):
        _, self.infos = self.reset(seed)
        assert isinstance(self.infos, list), 'PufferEnvs must return info as a list of dicts'

    def send(self, actions):
        _, _, _, _, self.infos = self.step(actions)
        assert isinstance(self.infos, list), 'PufferEnvs must return info as a list of dicts'

    def recv(self):
        return (self.observations, self.rewards, self.terminals,
            self.truncations, self.infos, self.agent_ids, self.masks)
