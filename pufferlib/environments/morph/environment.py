import time
import argparse
import functools
from pufferlib.environments.morph.humanoid_phc import HumanoidPHC
from pufferlib.environments.morph.render_env import HumanoidRenderEnv

import torch
import numpy as np

import pufferlib

def env_creator(name='morph'):
    return functools.partial(make, name)
 
def make(name, **kwargs):
    return PHCPufferEnv(name, **kwargs)

class PHCPufferEnv(pufferlib.PufferEnv):
    def __init__(self, name, motion_file, has_self_collision, num_envs=32, device_type="cuda",
            exp_name='morph', clip_actions=True, device_id=0, headless=True, log_interval=32):
        self.render_mode = 'native'
        cfg = {
            'env': {
                'num_envs': num_envs,
                'motion_file': motion_file,
            },
            'robot': {
                'has_self_collision': has_self_collision,
            },
            'exp_name': exp_name,
        }
        if headless:
            self.env = HumanoidPHC(cfg, device_type=device_type, device_id=device_id, headless=headless)
        else:
            self.env = HumanoidRenderEnv(cfg, device_type=device_type, device_id=device_id, headless=headless)

        self.single_observation_space = self.env.single_observation_space
        self.single_action_space = self.env.single_action_space
        self.num_agents = self.num_envs = self.env.num_envs
        self.clip_actions = clip_actions
        self.device = self.env.device

        # Check the buffer data types, match them to puffer
        buffers = pufferlib.namespace(
            observations=self.env.obs_buf,
            rewards=self.env.rew_buf,
            terminals=torch.zeros(self.num_agents, dtype=torch.bool, device=self.device),
            truncations=torch.zeros_like(self.env.reset_buf),
            masks=torch.ones_like(self.env.reset_buf),
            actions=torch.zeros(
                (self.num_agents, *self.single_action_space.shape), dtype=torch.float, device=self.device
            ),
        )

        super().__init__(buffers)

        self.rew_rms_norm = RunningMeanStd((1,), self.device)

        self.log_interval = log_interval
        self.episode_returns = torch.zeros(self.num_envs, dtype=torch.float32, device=self.device)
        self.episode_lengths = torch.zeros(self.num_envs, dtype=torch.int32, device=self.device)
        self._infos = {
            "episode_return": [],
            "episode_length": [],
        }

    def reset(self, seed=None):
        self.env.reset()
        self.demo = self.env.demo
        self.state = self.env.state
        self.tick = 0
        return self.observations, []

    def step(self, actions_np):
        if self.clip_actions:
            actions_np = np.clip(actions_np, -1, 1)
        self.actions[:] = torch.from_numpy(actions_np)

        # obs, reward, done are put into the buffers
        self.env.step(self.actions)
        self.demo = self.env.demo
        self.state = self.env.state

        self.terminals[:] = self.env.reset_buf
        done_indices = torch.nonzero(self.terminals).squeeze(-1)
        if len(done_indices) > 0:
            self.env.reset(done_indices)
            self._infos["episode_return"] += self.episode_returns[done_indices].tolist()
            self._infos["episode_length"] += self.episode_lengths[done_indices].tolist()
            self.episode_returns[done_indices] = 0
            self.episode_lengths[done_indices] = 0

        self.episode_returns[~self.terminals] += self.rewards[~self.terminals]
        self.episode_lengths[~self.terminals] += 1

        # TODO: self.env.extras has infos. Extract useful info?
        info = []
        self.tick += 1
        if self.tick % self.log_interval == 0:
            info = self.mean_and_log()

        # Simple reward scaling
        rew = self.rewards.clone() * 0.01

        return self.observations, rew, self.terminals, self.truncations, info

    def render(self):
        return self.env.render()

    def close(self):
        self.env.close()

    def mean_and_log(self):
        if len(self._infos["episode_return"]) < self.log_interval:
            return []

        info = {
            "episode_return": np.mean(self._infos["episode_return"]),
            "episode_length": np.mean(self._infos["episode_length"]),
        }
        self._infos["episode_return"].clear()
        self._infos["episode_length"].clear()

        return [info]


class RunningMeanStd:
    def __init__(self, insize, device, epsilon=1e-05, clip=5.0, scale=0.01):
        self.insize = insize
        self.epsilon = epsilon
        self.axis = [0]
        self.mean_size = insize[0]
        self.clip = clip
        self.scale = scale
        
        # Instead of register_buffer, just use regular tensors
        self.running_mean = torch.zeros(insize, dtype=torch.float32).to(device)
        self.running_var = torch.ones(insize, dtype=torch.float32).to(device)
        self.count = torch.ones((), dtype=torch.float32).to(device)
        self._frozen = False

    def freeze(self):
        self._frozen = True

    def unfreeze(self):
        self._frozen = False

    def __call__(self, input):
        y = (input - self.running_mean) / torch.sqrt(self.running_var + self.epsilon)
        y = torch.clamp(y, min=-self.clip, max=self.clip) * self.scale

        if not self._frozen:
            mean = input.mean(self.axis)
            var = input.var(self.axis)
            new_mean, new_var, new_count = update_mean_var_count_from_moments(
                self.running_mean, self.running_var, self.count, mean, var, input.size()[0]
            )
            self.running_mean, self.running_var, self.count = new_mean, new_var, new_count
            
        return y


@torch.jit.script
def update_mean_var_count_from_moments(mean, var, count, batch_mean, batch_var, batch_count):
    # type: (Tensor, Tensor, Tensor, Tensor, Tensor, int) -> Tuple[Tensor, Tensor, Tensor]
    delta = batch_mean - mean
    tot_count = count + batch_count
    new_mean = mean + delta * batch_count / tot_count
    m_a = var * count
    m_b = batch_var * batch_count
    M2 = m_a + m_b + delta**2 * count * batch_count / tot_count
    new_var = M2 / tot_count
    new_count = tot_count
    return new_mean, new_var, new_count




if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-n", "--num_envs", type=int, default=32)
    parser.add_argument("-m", "--motion_file", type=str, default="resources/morph/totalcapture_acting_poses.pkl")
    parser.add_argument("--disable_self_collision", action="store_true")
    args = parser.parse_args()

    def test_perf(env, timeout=10):
        steps = 0
        start = time.time()
        env.reset()
        actions = env.action_space.sample()

        print("Starting perf test...")
        while time.time() - start < timeout:
            env.step(actions)
            steps += env.num_agents

        end = time.time()
        sps = int(steps / (end - start))
        print(f"Steps: {steps}, SPS: {sps}")

    env = PHCPufferEnv(
        name = "morph",
        motion_file = args.motion_file,
        has_self_collision = not args.disable_self_collision,
        num_envs = args.num_envs,
    )
    test_perf(env)
