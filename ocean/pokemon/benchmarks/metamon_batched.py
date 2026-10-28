"""Batched released Metamon inference: one model forward for many concurrent games.

Each batch row is one game slot with its own KV-cache position (AMAGO's TformerHiddenState already keeps
per-row seq_lens), PrivateView, observation-space state, previous state/action, timestep and sampling
generator. A slot that must not act this turn (the other player is replacing a fainted Pokemon) still fills its
row with a copy of an acting row, but its cache position is not advanced, so its history is untouched: the
garbage key/value lands past its end and is overwritten by its next real step.

Per game this is the same computation as MetamonOpponent (same seed, same generator stream, same masking).
With batch 1 it is bit-identical; with larger batches GEMM reductions differ in the last bits, so a sampled
action can occasionally differ (see check in evaluate_batched.py --reference).
"""
import copy
import types

import numpy as np
import torch

from metamon_opponent import MetamonOpponent, MetamonMechanics
from private_view import PrivateView
from metamon.interface import UniversalState


def _masked_update(self):
    """TformerHiddenState.update restricted to rows in self.active (bool tensor)."""
    self.seq_lens += self.active.to(self.seq_lens.dtype)
    self.key_cache.roll_back(self.seq_lens)
    idxs = self.val_cache.roll_back(self.seq_lens)
    self.seq_lens[idxs] -= 1


class BatchedMetamon:
    def __init__(self, name, batch, threads=2, device='cuda'):
        base = MetamonOpponent(name=name, threads=threads, device=device)
        self.base, self.maker, self.policy, self.device = base, base.maker, base.policy, base.device
        self.checkpoint_sha256, self.action_count, self.batch = base.checkpoint_sha256, base.action_count, batch
        self.hidden = self.policy.init_hidden_state(batch, self.device)
        self.hidden.active = torch.ones(batch, dtype=torch.bool, device=self.device)
        self.hidden.update = types.MethodType(_masked_update, self.hidden)
        self.slots = [dict(view=PrivateView(MetamonMechanics()), space=copy.deepcopy(self.maker.observation_space),
                           generator=torch.Generator(device=self.device)) for _ in range(batch)]

    def reset(self, slot, seed):
        s = self.slots[slot]
        s['generator'].manual_seed(seed % (2**63-1))
        s['view'].reset()
        s['space'].reset()
        s.update(previous_state=None, previous_action=None, timestep=0)
        self.hidden.reset([slot])

    @torch.inference_mode()
    def choose(self, observations):
        """observations: {slot: 648-byte observation}. Returns {slot: native action}."""
        actions, rows = {}, []
        for slot, observation in observations.items():
            s = self.slots[slot]
            projected, mapping = s['view'].project(observation)
            if observation[6] == 0 and observation[490]:
                actions[slot] = 10  # other player replaces a fainted Pokemon; history does not advance
                continue
            state = UniversalState.from_dict(copy.deepcopy(projected))
            obs = s['space'].state_to_obs(state)
            illegal = np.ones(self.action_count, dtype=bool)
            for action in mapping: illegal[action] = False
            obs['illegal_actions'] = illegal
            rl2 = np.zeros(self.action_count+1, dtype=np.float32)
            if s['previous_state'] is not None:
                rl2[0] = self.maker.reward_function(s['previous_state'], state)
                rl2[1+s['previous_action']] = 1
            rows.append((slot, state, mapping, obs, rl2))
        if not rows: return actions
        index = {slot: i for i, (slot, *_) in enumerate(rows)}
        fill = [index.get(slot, 0) for slot in range(self.batch)]  # idle rows copy an acting row
        obs = {k: torch.as_tensor(np.stack([rows[i][3][k] for i in fill]), device=self.device).unsqueeze(1)
               for k in rows[0][3]}
        rl2 = torch.as_tensor(np.stack([rows[i][4] for i in fill]), device=self.device).unsqueeze(1)
        times = torch.tensor([self.slots[slot]['timestep'] for slot in range(self.batch)],
                             dtype=torch.long, device=self.device).view(-1, 1, 1)
        active = torch.zeros(self.batch, dtype=torch.bool)
        active[list(index)] = True
        self.hidden.active = active.to(self.device)
        embedding, self.hidden = self.policy.get_state_embedding(obs=obs, rl2s=rl2, time_idxs=times,
                                                                 hidden_state=self.hidden)
        distribution = self.policy.actor(embedding, straight_from_obs={k: obs[k] for k in self.policy.pass_obs_keys_to_actor})
        probabilities = distribution.probs[:, 0, -1].clone()
        probabilities[obs['illegal_actions'][:, 0]] = 0  # same final public legal mask as MetamonOpponent
        probabilities /= probabilities.sum(-1, keepdim=True)
        picks = torch.cat([torch.multinomial(probabilities[slot], 1, generator=self.slots[slot]['generator'])
                           for slot, *_ in rows]).tolist()
        for (slot, state, mapping, _, _), action in zip(rows, picks):
            if action not in mapping: raise RuntimeError(f'Metamon selected masked action {action}: {mapping}')
            s = self.slots[slot]
            s.update(previous_state=state, previous_action=action, timestep=s['timestep']+1)
            actions[slot] = mapping[action]
        self.last_probabilities = {slot: probabilities[slot] for slot, *_ in rows}
        return actions
