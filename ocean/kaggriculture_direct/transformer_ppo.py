"""Torch PPO fine-tuning of the selected compact Transformer in native self-play.

Complete fresh games, both neural seats, terminal WLD, a private paired critic,
and a frozen BC anchor evaluated on each sampled native action prefix.
"""
from __future__ import annotations

import argparse
import configparser
import ctypes as C
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace

import numpy as np
import torch
from torch import nn

from compact_contract import OBS, HEADS, LOGITS, STEPS
from transformer import (COMPILE_OPTIONS, CompactLoss, CompactTransformer, ModelConfig,
                         ROOT, evaluate, game_seeds)
from build_bc_dataset import load_bridge, validate_profile
import replay_native as native

FORMAT = "kag_compact_v7_transformer_ppo_v1"


@dataclass
class PPOConfig:
    total_timesteps: int = 1000000
    games_per_rollout: int = 16
    seed: int = 2026100900
    actor_learning_rate: float = 3e-6
    critic_learning_rate: float = 1e-3
    adam_epsilon: float = 1e-5
    critic_warmup_rollouts: int = 2
    critic_warmup_epochs: int = 4
    update_epochs: int = 2
    minibatch_size: int = 960
    gamma: float = 1.
    gae_lambda: float = .97
    clip_coef: float = .1
    value_clip: float = .2
    value_coef: float = 1.
    entropy_coef: float = .001
    teacher_kl_coef: float = .1
    target_kl: float = .03
    max_grad_norm: float = 1.
    eval_interval: int = 4
    device: str = "cuda"

    @classmethod
    def from_ini(cls, ini):
        result = cls(**{key: type(value)(ini["transformer_ppo"][key])
                        for key, value in asdict(cls()).items()})
        result.validate()
        return result

    def validate(self):
        for key in ("total_timesteps", "games_per_rollout", "critic_warmup_epochs",
                    "update_epochs", "minibatch_size", "eval_interval"):
            assert getattr(self, key) > 0, key
        assert self.critic_warmup_rollouts >= 0 and self.seed >= 0
        assert self.gamma == 1 and 0 < self.gae_lambda <= 1
        assert 0 < self.clip_coef < 1 and self.value_clip > 0
        for key in ("actor_learning_rate", "critic_learning_rate", "adam_epsilon",
                    "value_coef", "teacher_kl_coef", "target_kl", "max_grad_norm"):
            assert np.isfinite(getattr(self, key)) and getattr(self, key) > 0, key
        assert self.entropy_coef >= 0


class PairedCritic(nn.Module):
    """Shared score of each private same-state 128-feature seat view."""
    def __init__(self):
        super().__init__()
        self.score = nn.Sequential(nn.Linear(128, 256), nn.Tanh(),
                                   nn.Linear(256, 256), nn.Tanh(), nn.Linear(256, 1))
        nn.init.zeros_(self.score[-1].weight)
        nn.init.zeros_(self.score[-1].bias)

    def forward(self, obs):
        scores = self.score(obs[:, 3000:3256].float().reshape(-1, 2, 128)).squeeze(-1)
        return torch.tanh(.5 * (scores[:, 0] - scores[:, 1]))


class ActionDistribution(CompactLoss):
    def forward(self, logits, actions, masks, teacher_logits):
        support = masks[:, self.index] & self.width_mask
        logp = logits[:, self.index].masked_fill(~support, -1e9).log_softmax(-1)
        teacher = teacher_logits[:, self.index].masked_fill(~support, -1e9).log_softmax(-1)
        selected = logp.gather(-1, actions[..., None]).squeeze(-1).sum(-1)
        entropy = -(logp.exp() * logp).sum((-1, -2))
        # Conditional teacher || policy KL on the learner's stored prefixes.
        # Singleton/forced heads have logp=0 and contribute exactly zero.
        kl = (teacher.exp() * (teacher - logp)).sum((-1, -2))
        return selected, entropy, kl


def ppo_terms(logp, old_logp, advantage, value, old_value, returns,
              entropy, teacher_kl, config):
    log_ratio = logp - old_logp
    ratio = log_ratio.exp()
    policy = torch.maximum(-advantage * ratio,
        -advantage * ratio.clamp(1-config.clip_coef, 1+config.clip_coef))
    clipped_value = old_value + (value-old_value).clamp(-config.value_clip, config.value_clip)
    value_loss = .5 * torch.maximum((value-returns).square(), (clipped_value-returns).square())
    loss = policy + config.value_coef*value_loss - config.entropy_coef*entropy + config.teacher_kl_coef*teacher_kl
    approximate_kl = (torch.expm1(log_ratio)-log_ratio).clamp_min(0)
    return loss, torch.stack((policy, value_loss, entropy, teacher_kl, approximate_kl,
                             ((ratio-1).abs() > config.clip_coef).float()), -1)


class PPOObjective(nn.Module):
    def __init__(self, critic, config):
        super().__init__()
        self.critic, self.config = critic, config
        self.distribution = ActionDistribution().to(next(critic.parameters()).device)

    def forward(self, logits, obs, actions, masks, teacher_logits, old_logp, advantage, returns, old_value):
        logp, entropy, kl = self.distribution(logits, actions, masks, teacher_logits)
        return ppo_terms(logp, old_logp, advantage, self.critic(obs), old_value,
                         returns, entropy, kl, self.config)


def gae(rewards, values, dones, gamma, lam):
    """Time-major GAE with a zero bootstrap after each complete terminal game."""
    assert rewards.shape == values.shape == dones.shape and dones[-1].all()
    advantage = np.zeros_like(rewards)
    carry, next_value = np.zeros(rewards.shape[1], np.float32), np.zeros(rewards.shape[1], np.float32)
    for t in range(len(rewards)-1, -1, -1):
        live = 1-dones[t].astype(np.float32)
        delta = rewards[t] + gamma*next_value*live - values[t]
        carry = delta + gamma*lam*live*carry
        advantage[t], next_value = carry, values[t]
    return advantage, advantage + values


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_actor(path, device):
    saved = torch.load(path, map_location=device, weights_only=True)
    assert saved["format"] in ("kag_compact_v7_transformer_v2", FORMAT)
    model = CompactTransformer(ModelConfig(**saved["config"])).to(device)
    model.load_state_dict(saved["model"])
    return model, saved


def selfplay_bridge():
    lib = load_bridge(ROOT / "ocean/kaggriculture_direct/build/replay.so")
    ptr = C.c_void_p
    lib.kag_direct_selfplay_observe.argtypes = [ptr, ptr]
    lib.kag_direct_selfplay_step.argtypes = [ptr, ptr, ptr, ptr, ptr, ptr]
    return lib


def collect_rollout(lib, actor, teacher, critic, actor_forward, teacher_forward,
                    distribution, config, seeds, microbatch):
    device = next(actor.parameters()).device
    rows, horizon = 2*len(seeds), STEPS-1
    obs = np.empty((horizon, rows, OBS), np.float32)
    actions = np.empty((horizon, rows, HEADS), np.int64)
    masks = np.empty((horizon, rows, LOGITS), np.bool_)
    behavior_logits = np.empty((horizon, rows, LOGITS), np.float32)
    teacher_logits = np.empty_like(behavior_logits)
    values, rewards = np.empty((horizon, rows), np.float32), np.empty((horizon, rows), np.float32)
    heads = np.empty((rows, HEADS), np.float32)
    random = np.array([[int(seed) ^ 73, int(seed) ^ 74] for seed in seeds], np.uint32)
    contexts, money = [], []
    actor.eval(); teacher.eval(); critic.eval()
    started = time.monotonic()
    try:
        for seed in seeds:
            native_config = native.CConfig()
            lib.kg_config_default(C.byref(native_config))
            native_config.seed = int(seed)
            context = lib.kag_bc_create(C.byref(native_config), str(ROOT / "config/kaggriculture.ini").encode())
            assert context
            contexts.append(context)
        with torch.no_grad():
            for t in range(horizon):
                for i, context in enumerate(contexts):
                    assert lib.kag_direct_selfplay_observe(context, obs[t, 2*i].ctypes.data)
                tensor = torch.from_numpy(obs[t]).to(device)
                # Use the SAME grad-enabled actor graph as PPO, then discard
                # activations. Inference-only fusion changed the BF16 behavior
                # distribution enough to trigger clipping before any update.
                with torch.enable_grad(), torch.autocast(device.type, dtype=torch.bfloat16, enabled=device.type == "cuda"):
                    acting_logits = actor_forward(tensor)
                    behavior_logits[t] = acting_logits.detach().cpu().numpy()
                    del acting_logits
                with torch.autocast(device.type, dtype=torch.bfloat16, enabled=device.type == "cuda"):
                    teacher_logits[t] = teacher_forward(tensor).cpu().numpy()
                values[t] = critic(tensor).cpu().numpy()
                assert np.isfinite(behavior_logits[t]).all() and np.isfinite(teacher_logits[t]).all()
                for i, context in enumerate(contexts):
                    assert lib.kag_direct_selfplay_step(context, behavior_logits[t, 2*i].ctypes.data,
                        random[i].ctypes.data, heads[2*i].ctypes.data,
                        masks[t, 2*i].ctypes.data, rewards[t, 2*i:2*i+2].ctypes.data)
                actions[t] = heads
                if (t+1) % 240 == 0:
                    print(f"PPO_ROLLOUT_PROGRESS turns={t+1}/{horizon} seconds={time.monotonic()-started:.1f}", flush=True)
        for context in contexts:
            state = lib.kag_bc_state(context)
            assert lib.kg_done(state)
            money.append([lib.kg_player_money(state, seat) for seat in (0, 1)])
    finally:
        for context in contexts:
            lib.kag_bc_destroy(context)
    # The bridge is deliberately full-game/fresh-reset only. No PPO truncation
    # bootstrap, rule bot, replay reset, or opponent-history policy is hidden here.
    assert not rewards[:-1].any() and np.array_equal(rewards[:, 0::2], -rewards[:, 1::2])
    expected = np.sign(np.asarray(money)[:, 0]-np.asarray(money)[:, 1])
    np.testing.assert_array_equal(rewards[-1, 0::2], expected)
    dones = np.zeros_like(rewards, dtype=bool); dones[-1] = True
    advantages, returns = gae(rewards, values, dones, config.gamma, config.gae_lambda)
    result = dict(obs=obs.reshape(-1, OBS), actions=actions.reshape(-1, HEADS),
        masks=masks.reshape(-1, LOGITS), teacher_logits=teacher_logits.reshape(-1, LOGITS),
        old_value=values.reshape(-1), advantage=advantages.reshape(-1), returns=returns.reshape(-1),
        mc_returns=np.broadcast_to(rewards[-1], rewards.shape).reshape(-1).copy())
    old_logp = np.empty(horizon*rows, np.float32)
    flat_logits = behavior_logits.reshape(-1, LOGITS)
    with torch.no_grad():
        for start in range(0, len(old_logp), microbatch):
            end = start+microbatch
            logits = torch.from_numpy(flat_logits[start:end]).to(device)
            selected, _, _ = distribution(logits,
                torch.from_numpy(result["actions"][start:end]).to(device),
                torch.from_numpy(result["masks"][start:end]).to(device), logits)
            old_logp[start:end] = selected.cpu().numpy()
    result["old_logp"] = old_logp
    summary = dict(games=len(seeds), agent_steps=len(old_logp), cash=float(np.mean(money)),
        decisive_games=int(np.count_nonzero(expected)), seconds=time.monotonic()-started)
    return result, summary


def warm_critic(critic, optimizer, rollout, config, rng, device):
    critic.train()
    total = []
    for _ in range(config.critic_warmup_epochs):
        order = rng.permutation(len(rollout["obs"]))
        for start in range(0, len(order), config.minibatch_size):
            index = order[start:start+config.minibatch_size]
            obs = torch.from_numpy(rollout["obs"][index]).to(device)
            target = torch.from_numpy(rollout["mc_returns"][index]).to(device)
            loss = .5*(critic(obs)-target).square().mean()
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(critic.parameters(), config.max_grad_norm, error_if_nonfinite=True)
            optimizer.step()
            total.append(loss.item())
    return dict(critic_warmup_loss=float(np.mean(total)), actor_optimizer_steps=0)


def update_policy(actor, actor_forward, critic, actor_opt, critic_opt, objective, rollout, config, rng, device, microbatch):
    actor.train(); critic.train()
    advantage = rollout["advantage"]
    rollout["advantage"] = (advantage-advantage.mean()) / max(float(advantage.std()), 1e-8)
    totals, updates, early_stop, initial_kl = np.zeros(6), 0, False, None
    fields = ("obs", "actions", "masks", "teacher_logits", "old_logp", "advantage", "returns", "old_value")
    for _ in range(config.update_epochs):
        order = rng.permutation(len(advantage))
        for start in range(0, len(order), config.minibatch_size):
            index = order[start:start+config.minibatch_size]
            actor_opt.zero_grad(set_to_none=True); critic_opt.zero_grad(set_to_none=True)
            stats = torch.zeros(6, device=device)
            for first in range(0, len(index), microbatch):
                batch = index[first:first+microbatch]
                tensors = [torch.from_numpy(rollout[key][batch]).to(device) for key in fields]
                with torch.autocast(device.type, dtype=torch.bfloat16, enabled=device.type == "cuda"):
                    logits = actor_forward(tensors[0])
                loss, metrics = objective(logits, *tensors)
                (loss.sum()/len(index)).backward()
                stats += metrics.detach().sum(0)/len(index)
            stats = stats.cpu().numpy()
            assert np.isfinite(stats).all(), "nonfinite PPO statistics"
            if initial_kl is None:
                initial_kl = float(stats[4])
                assert initial_kl < 1e-5 and stats[5] < .001, "behavior/update log-probability mismatch before first PPO step"
            if stats[4] > config.target_kl:
                early_stop = True
                actor_opt.zero_grad(set_to_none=True); critic_opt.zero_grad(set_to_none=True)
                break
            torch.nn.utils.clip_grad_norm_(actor.parameters(), config.max_grad_norm, error_if_nonfinite=True)
            torch.nn.utils.clip_grad_norm_(critic.parameters(), config.max_grad_norm, error_if_nonfinite=True)
            actor_opt.step(); critic_opt.step()
            totals += stats; updates += 1
        if early_stop:
            break
    names = ("policy_loss", "value_loss", "entropy", "teacher_kl", "approximate_kl", "clip_fraction")
    return dict(zip(names, (totals/max(updates, 1)).tolist()),
                actor_optimizer_steps=updates, early_stop=early_stop, initial_approximate_kl=initial_kl,
                early_stop_kl=float(stats[4]) if early_stop else None)


def save_checkpoint(path, actor, critic, actor_opt, critic_opt, rng, counters, best,
                    teacher_path, teacher_hash, config, runtime_contract):
    payload = dict(format=FORMAT, config=asdict(actor.config), model=actor.state_dict(),
        critic=critic.state_dict(), actor_optimizer=actor_opt.state_dict(),
        critic_optimizer=critic_opt.state_dict(), numpy_rng_state=rng.bit_generator.state,
        torch_rng_state=torch.get_rng_state(), counters=counters.copy(), best=best.copy(),
        teacher_checkpoint=str(teacher_path), teacher_sha256=teacher_hash, ppo_config=asdict(config),
        runtime_contract=runtime_contract)
    if next(actor.parameters()).is_cuda:
        payload["cuda_rng_state"] = torch.cuda.get_rng_state_all()
    temporary = path.with_suffix(".pt.tmp")
    torch.save(payload, temporary); temporary.replace(path)


def main():
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(ROOT / "config/kaggriculture.ini")
    config = PPOConfig.from_ini(ini)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("train", "smoke"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--resume", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--total-timesteps", type=int)
    parser.add_argument("--games", type=int)
    parser.add_argument("--device")
    parser.add_argument("--compile", action=argparse.BooleanOptionalAction,
                        default=ini.getboolean("transformer", "compile"))
    args = parser.parse_args()
    if args.mode == "smoke":
        config.games_per_rollout, config.critic_warmup_rollouts = 2, 1
        config.critic_warmup_epochs, config.update_epochs, config.eval_interval = 2, 1, 1
        config.total_timesteps = 2 * (STEPS-1) * 2 * config.games_per_rollout
    for arg, name in ((args.total_timesteps, "total_timesteps"), (args.games, "games_per_rollout"), (args.device, "device")):
        if arg is not None:
            setattr(config, name, arg)
    config.validate()
    assert config.total_timesteps > config.critic_warmup_rollouts*config.games_per_rollout*2*(STEPS-1), "budget must include PPO after critic warmup"
    microbatch = ini.getint("transformer", "microbatch")
    assert microbatch > 0
    teacher_path = (ROOT / ini["transformer"]["checkpoint"]).resolve()
    teacher_hash = ini["transformer"]["checkpoint_sha256"]
    evaluation = SimpleNamespace(eval_games=2 if args.mode == "smoke" else ini.getint("transformer", "eval_games"),
        eval_seed=ini.getint("transformer", "eval_seed"), map_seed_mode=ini["transformer"]["eval_map_seed_mode"], microbatch=microbatch)
    runtime_contract = dict(environment=dict(ini["env"]), action_version=7, critic_mode=2,
        screening={k:v for k,v in vars(evaluation).items() if k != "microbatch"},
        validation_games=2 if args.mode == "smoke" else ini.getint("transformer", "validation_games"),
        validation_seed=ini.getint("transformer", "validation_seed"))
    output = args.output or ROOT / ini["transformer"]["output_root"] / f"ppo_{args.mode}_{time.time_ns()}"
    output = output.resolve()
    planned = dict(ppo=asdict(config), actor_checkpoint=str(teacher_path), teacher_sha256=teacher_hash,
        output=str(output), compiled=args.compile, compile_options=COMPILE_OPTIONS if args.compile else None,
        microbatch=microbatch, opponents="current-policy self-play, both seats", fresh_resets=True,
        actor_features=3000, private_critic_features=256, action_version=7,
        evaluation=vars(evaluation), runtime_contract=runtime_contract,
        resume=str(args.resume) if args.resume else None)
    validate_profile(ROOT / "config/kaggriculture.ini")
    assert ini.getint("policy", "action_version") == 7, "PPO requires compact action ABI 7"
    assert ini.getint("policy", "critic_mode") == 2, "PPO requires the paired private critic observations"
    if args.dry_run:
        print(json.dumps(planned, indent=2)); return
    assert digest(teacher_path) == teacher_hash, "selected BC checkpoint checksum mismatch"
    # This initial bounded budget uses fresh seeds disjoint from evaluation.
    training_games = ((config.total_timesteps+2*(STEPS-1)*config.games_per_rollout-1)//
                      (2*(STEPS-1)*config.games_per_rollout))*config.games_per_rollout
    reserved = set(game_seeds(evaluation.eval_seed, evaluation.eval_games, evaluation.map_seed_mode))
    reserved.update(game_seeds(ini.getint("transformer", "validation_seed"), ini.getint("transformer", "validation_games"), "explicit"))
    assert not any(config.seed <= seed < config.seed+training_games for seed in reserved)
    assert config.seed+training_games < 2**32
    output.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(4); torch.manual_seed(config.seed)
    device = torch.device(config.device)
    rng = np.random.default_rng(config.seed)
    actor, saved = load_actor(args.resume or teacher_path, device)
    teacher, _ = load_actor(teacher_path, device)
    assert actor.config == teacher.config, "actor/anchor architecture mismatch"
    teacher.requires_grad_(False); teacher.eval()
    critic = PairedCritic().to(device)
    fused = ini.getboolean("transformer", "fused_adam") and device.type == "cuda"
    actor_opt = torch.optim.Adam(actor.parameters(), lr=config.actor_learning_rate, eps=config.adam_epsilon, fused=fused)
    critic_opt = torch.optim.Adam(critic.parameters(), lr=config.critic_learning_rate, eps=config.adam_epsilon, fused=fused)
    counters = dict(rollouts=0, agent_steps=0, actor_optimizer_steps=0)
    best = None
    if args.resume:
        assert saved["format"] == FORMAT, "PPO resume needs a PPO checkpoint; the configured BC starts fresh critic/optimizers"
        assert saved["teacher_sha256"] == teacher_hash
        assert saved["runtime_contract"] == runtime_contract, "PPO environment/evaluation contract mismatch"
        # A longer terminal budget is the only automatic recipe change on resume.
        prior = dict(saved["ppo_config"]); prior["total_timesteps"] = config.total_timesteps
        assert prior == asdict(config), "PPO resume recipe mismatch"
        critic.load_state_dict(saved["critic"])
        actor_opt.load_state_dict(saved["actor_optimizer"]); critic_opt.load_state_dict(saved["critic_optimizer"])
        rng.bit_generator.state = saved["numpy_rng_state"]
        torch.set_rng_state(saved["torch_rng_state"].cpu())
        if device.type == "cuda":
            torch.cuda.set_rng_state_all([s.cpu() for s in saved["cuda_rng_state"]])
        counters, best = saved["counters"], saved["best"]
        assert counters["agent_steps"] < config.total_timesteps
        assert Path(best["checkpoint"]).is_file()
    objective = PPOObjective(critic, config).to(device)
    actor_forward, teacher_forward = actor, teacher
    if args.compile:
        objective = torch.compile(objective, fullgraph=True, dynamic=False, options=COMPILE_OPTIONS)
        actor_forward = torch.compile(actor, fullgraph=True, dynamic=False, options=COMPILE_OPTIONS)
        teacher_forward = torch.compile(teacher, fullgraph=True, dynamic=False, options=COMPILE_OPTIONS)
    distribution = ActionDistribution().to(device)
    lib = selfplay_bridge()
    planned.update(source_sha256=digest(Path(__file__)), actor_source_sha256=digest(Path(__file__).with_name("transformer.py")),
        bridge_sha256=digest(ROOT / "ocean/kaggriculture_direct/build/replay.so"), torch=torch.__version__,
        shared_config=(ROOT / "config/kaggriculture.ini").read_text())
    with (output / "config.json").open("x") as stream:
        json.dump(planned, stream, indent=2)
    print("PPO_CONFIG " + json.dumps({k:v for k,v in planned.items() if k != "shared_config"}), flush=True)
    if device.type == "cuda":
        torch.cuda.reset_peak_memory_stats()
    started = time.monotonic()
    if best is None:
        baseline = evaluate(actor, evaluation, output / "baseline_eval.json", device)
        best = dict(checkpoint=str(teacher_path), cash=baseline["cash"], rollout=0, source="BC epoch 19")
    with (output / "selected.json").open("w") as stream:
        json.dump(best, stream, indent=2)
    while counters["agent_steps"] < config.total_timesteps:
        index = counters["rollouts"]
        seeds = range(config.seed+index*config.games_per_rollout, config.seed+(index+1)*config.games_per_rollout)
        rollout, metrics = collect_rollout(lib, actor, teacher, critic, actor_forward, teacher_forward,
                                          distribution, config, seeds, microbatch)
        warmup = index < config.critic_warmup_rollouts
        if warmup:
            metrics.update(warm_critic(critic, critic_opt, rollout, config, rng, device))
        else:
            metrics.update(update_policy(actor, actor_forward, critic, actor_opt, critic_opt, objective,
                                         rollout, config, rng, device, microbatch))
        counters["rollouts"] += 1
        counters["agent_steps"] += metrics["agent_steps"]
        counters["actor_optimizer_steps"] += metrics["actor_optimizer_steps"]
        metrics.update(rollout=counters["rollouts"], total_agent_steps=counters["agent_steps"],
                       phase="critic_warmup" if warmup else "ppo", elapsed_seconds=time.monotonic()-started,
                       peak_gpu_bytes=torch.cuda.max_memory_allocated() if device.type == "cuda" else 0)
        assert all(torch.isfinite(p).all() for p in actor.parameters())
        assert all(torch.isfinite(p).all() for p in critic.parameters())
        assert all(p.grad is None for p in teacher.parameters())
        print("PPO_METRICS " + json.dumps(metrics), flush=True)
        with (output / "metrics.jsonl").open("a") as stream:
            stream.write(json.dumps(metrics)+"\n")
        save_checkpoint(output / "latest.pt", actor, critic, actor_opt, critic_opt, rng, counters,
                        best, teacher_path, teacher_hash, config, runtime_contract)
        if not warmup and (counters["rollouts"] % config.eval_interval == 0 or counters["agent_steps"] >= config.total_timesteps):
            result = evaluate(actor, evaluation, output / f"eval_{counters['rollouts']}.json", device)
            if result["cash"] > best["cash"]:
                best = dict(checkpoint=str(output / "best.pt"), cash=result["cash"], rollout=counters["rollouts"], source="PPO")
                save_checkpoint(output / "best.pt", actor, critic, actor_opt, critic_opt, rng, counters,
                                best, teacher_path, teacher_hash, config, runtime_contract)
                save_checkpoint(output / "latest.pt", actor, critic, actor_opt, critic_opt, rng, counters,
                                best, teacher_path, teacher_hash, config, runtime_contract)
                with (output / "selected.json").open("w") as stream:
                    json.dump(best, stream, indent=2)
        del rollout
    assert counters["actor_optimizer_steps"] > 0, "run completed without a PPO update"
    assert digest(teacher_path) == teacher_hash, "frozen BC checkpoint changed"
    selected = torch.load(best["checkpoint"], map_location=device, weights_only=True)
    actor.load_state_dict(selected["model"])
    validation = SimpleNamespace(**vars(evaluation))
    validation.eval_games = 2 if args.mode == "smoke" else ini.getint("transformer", "validation_games")
    validation.eval_seed = ini.getint("transformer", "validation_seed")
    validation.map_seed_mode = "explicit"
    evaluate(actor, validation, output / "selected_validation.json", device)
    print("PPO_COMPLETE " + json.dumps(dict(counters, seconds=time.monotonic()-started, selected=best)), flush=True)


if __name__ == "__main__":
    main()
