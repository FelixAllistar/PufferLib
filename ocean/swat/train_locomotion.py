#!/usr/bin/env python3
"""Train a bounded local movement policy in the authoritative Box3D game.

Initial supervised warm-start, followed by PPO on actual goal progress. Fixed
validation seeds select the checkpoint; separate untouched test seeds evaluate
each curriculum stage and both actor roles. This is a
locomotion baseline, not trained squad tactics or combat.
"""
import argparse
import ctypes as ct
import json
from pathlib import Path
import random
import time
import numpy as np
import torch
from torch import nn

ROOT = Path(__file__).resolve().parents[2]
SIZES = (5, 3, 3, 2, 2, 3)
STAGES = ('goal', 'stairs', 'clearance', 'door', 'breached_wall')

class Game:
    def __init__(self, library, seed, role, stage):
        self.lib = library
        self.env = library.swat_training_create(seed, role, stage)
        if not self.env:
            raise RuntimeError('Box3D curriculum allocation failed')
        self.obs = np.empty(32, dtype=np.float32)
        self.reward = ct.c_float()
        self.reset(seed, role, stage)
    def reset(self, seed, role, stage):
        self.lib.swat_training_reset(self.env, seed, role, stage)
        self.lib.swat_training_observe(self.env, self.obs.ctypes.data_as(ct.POINTER(ct.c_float)))
        return self.obs.copy()
    def step(self, action):
        action = np.ascontiguousarray(action, dtype=np.float32)
        status = self.lib.swat_training_step(self.env, action.ctypes.data_as(ct.POINTER(ct.c_float)), self.obs.ctypes.data_as(ct.POINTER(ct.c_float)), ct.byref(self.reward))
        return self.obs.copy(), self.reward.value, status
    def close(self):
        self.lib.swat_training_close(self.env)

class Policy(nn.Module):
    def __init__(self):
        super().__init__()
        self.encoder = nn.Linear(32, 64)
        self.actor = nn.Linear(64, sum(SIZES))
        self.critic = nn.Linear(64, 1)
    def forward(self, x):
        hidden = torch.tanh(self.encoder(x))
        return self.actor(hidden), self.critic(hidden).squeeze(-1)
    def action(self, x, actions=None, deterministic=False):
        logits, value = self(x)
        distributions = [torch.distributions.Categorical(logits=head) for head in logits.split(SIZES, -1)]
        if actions is None:
            actions = torch.stack([d.logits.argmax(-1) if deterministic else d.sample() for d in distributions], -1)
        probability = sum(d.log_prob(actions[..., i]) for i, d in enumerate(distributions))
        entropy = sum(d.entropy() for d in distributions)
        return actions, probability, entropy, value

def teacher(obs):
    error = np.arctan2(obs[13], obs[12])
    yaw = 0 if error < -.07 else 1 if error < -.015 else 4 if error > .07 else 3 if error > .015 else 2
    crouch = int(obs[26] < .65 and obs[21] > obs[26] + .08)
    return np.array([yaw, 2 if abs(error) < .9 else 1, 1, crouch, 0, 0], np.int64)

def evaluate(lib, model, seeds, teacher_policy=False):
    report = {}
    for stage, name in enumerate(STAGES):
        wins, ticks, count = 0, 0, 0
        for role in (0, 1):
            game = Game(lib, seeds[0], role, stage)
            try:
                for seed in seeds:
                    obs = game.reset(seed, role, stage)
                    for step in range(300):
                        if teacher_policy:
                            action = teacher(obs)
                        else:
                            with torch.no_grad():
                                action = model.action(torch.from_numpy(obs)[None], deterministic=True)[0][0].numpy()
                        obs, reward, status = game.step(action)
                        if status:
                            break
                    wins += status == 1
                    ticks += (step + 1) * 4
                    count += 1
            finally:
                game.close()
        report[name] = {'successes': wins, 'episodes': count, 'success_rate': wins / count, 'mean_ticks': ticks / count}
    return report

def export(model, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    weights = np.concatenate([model.encoder.weight.detach().numpy().ravel(), model.encoder.bias.detach().numpy(), model.actor.weight.detach().numpy().ravel(), model.actor.bias.detach().numpy()])
    with path.open('w') as f:
        f.write('SWAT_LOCOMOTION 1 32 64 18\n')
        for weight in weights:
            f.write(f'{weight:.9g}\n')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--library', type=Path, default=ROOT / 'build/swat/libswat_training.so')
    ap.add_argument('--output', type=Path, default=ROOT / 'build/swat/training/locomotion-v1')
    ap.add_argument('--seed', type=int, default=2718)
    ap.add_argument('--demo-seeds', type=int, default=6)
    ap.add_argument('--epochs', type=int, default=60)
    ap.add_argument('--updates', type=int, default=8)
    ap.add_argument('--eval-seeds', type=int, default=4)
    args = ap.parse_args()
    torch.set_num_threads(2)
    torch.manual_seed(args.seed); np.random.seed(args.seed); random.seed(args.seed)
    lib = ct.CDLL(str(args.library.resolve()))
    fptr = ct.POINTER(ct.c_float)
    lib.swat_training_create.argtypes = [ct.c_uint32, ct.c_int, ct.c_int]; lib.swat_training_create.restype = ct.c_void_p
    lib.swat_training_reset.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_int, ct.c_int]
    lib.swat_training_observe.argtypes = [ct.c_void_p, fptr]
    lib.swat_training_step.argtypes = [ct.c_void_p, fptr, fptr, fptr]; lib.swat_training_step.restype = ct.c_int
    lib.swat_training_close.argtypes = [ct.c_void_p]
    model = Policy()
    held_out = [1000003 + i * 7919 for i in range(args.eval_seeds)]
    start = time.monotonic()
    before = evaluate(lib, model, held_out)
    baseline = evaluate(lib, model, held_out, teacher_policy=True)
    print('Held-out untrained and scripted baselines recorded', flush=True)
    xs, ys = [], []
    for stage in range(5):
        for role in (0, 1):
            game = Game(lib, args.seed, role, stage)
            try:
                for i in range(args.demo_seeds):
                    obs = game.reset(args.seed + 1009 * i + stage * 97 + role, role, stage)
                    for step in range(300):
                        action = teacher(obs)
                        xs.append(obs); ys.append(action)
                        obs, reward, status = game.step(action)
                        if status:
                            break
            finally:
                game.close()
    x, y = torch.tensor(np.array(xs)), torch.tensor(np.array(ys))
    optimizer = torch.optim.Adam(model.parameters(), lr=.001)
    for epoch in range(args.epochs):
        for indices in torch.randperm(len(x)).split(512):
            logits, _ = model(x[indices])
            loss = sum(nn.functional.cross_entropy(head, y[indices, i]) for i, head in enumerate(logits.split(SIZES, -1)))
            optimizer.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(model.parameters(), 1); optimizer.step()
        if (epoch + 1) % 20 == 0:
            print(f'Warm-start epoch {epoch+1}: loss={loss.item():.4f}, real game samples={len(x)}', flush=True)
    warm = evaluate(lib, model, held_out)
    # Preserve the best evaluated checkpoint; PPO may expose curriculum gaps.
    best = {k: v.detach().clone() for k, v in model.state_dict().items()}
    best_score = sum(r['success_rate'] for r in warm.values())
    games = [Game(lib, args.seed + i, i % 2, i % 5) for i in range(8)]
    obs = np.stack([g.obs for g in games])
    optimizer = torch.optim.Adam(model.parameters(), lr=.0001)
    transitions = 0
    try:
        for update in range(args.updates):
            observations, actions, old_logs, rewards, dones, values = [], [], [], [], [], []
            for step in range(64):
                with torch.no_grad():
                    action, log, _, value = model.action(torch.tensor(obs))
                observations.append(obs.copy()); actions.append(action); old_logs.append(log); values.append(value)
                reward, done = [], []
                for i, game in enumerate(games):
                    result, r, status = game.step(action[i].numpy()); reward.append(r); done.append(bool(status))
                    obs[i] = game.reset(args.seed + transitions + i + 1, i % 2, (i + update) % 5) if status else result
                rewards.append(torch.tensor(reward)); dones.append(torch.tensor(done)); transitions += len(games)
            with torch.no_grad():
                _, bootstrap = model(torch.tensor(obs))
            advantages = torch.zeros(64, len(games)); gae = torch.zeros(len(games))
            for t in reversed(range(64)):
                live = (~dones[t]).float()
                following = bootstrap if t == 63 else values[t+1]
                delta = rewards[t] + .995 * following * live - values[t]
                gae = delta + .995 * .95 * live * gae; advantages[t] = gae
            returns = advantages + torch.stack(values)
            xx = torch.tensor(np.array(observations)).flatten(0, 1); aa = torch.stack(actions).flatten(0, 1)
            logs = torch.stack(old_logs).flatten(); rr = returns.flatten(); adv = advantages.flatten()
            adv = (adv - adv.mean()) / (adv.std() + 1e-8)
            for epoch in range(4):
                for indices in torch.randperm(len(xx)).split(256):
                    _, log, entropy, value = model.action(xx[indices], aa[indices])
                    ratio = (log - logs[indices]).exp()
                    actor_loss = -torch.minimum(ratio * adv[indices], ratio.clamp(.8, 1.2) * adv[indices]).mean()
                    loss = actor_loss + .5 * (value - rr[indices]).square().mean() - .002 * entropy.mean()
                    optimizer.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(model.parameters(), .5); optimizer.step()
            print(f'PPO update {update+1}/{args.updates}: {transitions} authoritative transitions', flush=True)
    finally:
        for game in games:
            game.close()
    ppo = evaluate(lib, model, held_out)
    if sum(r['success_rate'] for r in ppo.values()) < best_score:
        model.load_state_dict(best)
        selection = 'warm_start'
        selected = warm
    else:
        selection = 'ppo'
        selected = ppo
    test_seeds = [2000003 + i * 7907 for i in range(args.eval_seeds)]
    test = evaluate(lib, model, test_seeds)
    test_baseline = evaluate(lib, model, test_seeds, teacher_policy=True)
    args.output.mkdir(parents=True, exist_ok=True)
    export(model, args.output / 'policy.txt')
    torch.save(model.state_dict(), args.output / 'checkpoint.pt')
    report = {'contract': 1, 'seed': args.seed, 'cpu_threads': 2, 'validation_seeds': held_out, 'warm_start_samples': len(x), 'ppo_transitions': transitions, 'before': before, 'scripted_baseline': baseline, 'warm_start': warm, 'ppo': ppo, 'selected': selection, 'after': selected, 'test_seeds': test_seeds, 'test': test, 'test_scripted_baseline': test_baseline, 'elapsed_seconds': time.monotonic()-start, 'default_enabled': False}
    (args.output / 'evaluation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'selected': selection, 'after': selected, 'test_seeds': test_seeds, 'test': test, 'test_scripted_baseline': test_baseline, 'elapsed_seconds': report['elapsed_seconds']}, indent=2), flush=True)

if __name__ == '__main__':
    main()
