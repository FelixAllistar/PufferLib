import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import pytest

torch = pytest.importorskip("torch")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compact_contract import HEADS, LOGITS, SIZES
from transformer_ppo import (ActionDistribution, PairedCritic, PPOConfig, collect_rollout,
                             gae, ppo_terms, selfplay_bridge)

ROOT = Path(__file__).resolve().parents[3]


def test_conditional_joint_likelihood_entropy_and_teacher_kl_match_head_oracle():
    torch.manual_seed(41)
    logits = torch.randn(3, LOGITS, requires_grad=True)
    teacher = torch.randn_like(logits)
    actions = torch.zeros(3, HEADS, dtype=torch.long)
    mask = torch.zeros_like(logits, dtype=torch.bool)
    expected = [torch.zeros(3) for _ in range(3)]
    offset = 0
    for h, size in enumerate(SIZES):
        count = 1 if h % 3 == 0 else min(size, 7)
        mask[:, offset:offset+count] = True
        actions[:, h] = count-1
        logp = logits[:, offset:offset+count].log_softmax(-1)
        old = teacher[:, offset:offset+count].log_softmax(-1)
        expected[0] = expected[0] + logp[:, -1]
        expected[1] = expected[1] - (logp.exp()*logp).sum(-1)
        expected[2] = expected[2] + (old.exp()*(old-logp)).sum(-1)
        offset += size
    actual = ActionDistribution()(logits, actions, mask, teacher)
    for value, reference in zip(actual, expected):
        torch.testing.assert_close(value, reference)
    sum(value.sum() for value in actual).backward()
    assert torch.isfinite(logits.grad).all() and not logits.grad[~mask].any()
    offset = 0
    for h, size in enumerate(SIZES):
        if h % 3 == 0:
            assert not logits.grad[:, offset:offset+size].any()
        offset += size
    _, _, anchor_kl = ActionDistribution()(logits.detach(), actions, mask, logits.detach())
    assert not anchor_kl.any()


def test_ppo_clip_signs_value_clipping_and_kl():
    config = PPOConfig()
    ratio = torch.tensor([1.5, .5])
    loss, stats = ppo_terms(ratio.log(), torch.zeros(2), torch.tensor([1., -1.]),
        torch.tensor([.5, .5]), torch.zeros(2), torch.ones(2),
        torch.zeros(2), torch.zeros(2), config)
    torch.testing.assert_close(stats[:, 0], torch.tensor([-1.1, .9]))
    torch.testing.assert_close(stats[:, 1], torch.full((2,), .32))
    torch.testing.assert_close(stats[:, 4], ratio-1-ratio.log())
    torch.testing.assert_close(loss, stats[:, 0]+stats[:, 1])
    assert (stats[:, 5] == 1).all()


def test_gae_does_not_cross_terminal_and_is_zero_sum():
    rewards = np.array([[0, 0], [1, -1], [0, 0], [-1, 1]], np.float32)
    values = np.array([[.2, -.2], [.1, -.1], [.3, -.3], [.4, -.4]], np.float32)
    done = np.array([[0, 0], [1, 1], [0, 0], [1, 1]], bool)
    advantages, returns = gae(rewards, values, done, 1, 1)
    expected = np.array([[1, -1], [1, -1], [-1, 1], [-1, 1]], np.float32)
    np.testing.assert_allclose(returns, expected, atol=1e-7)
    np.testing.assert_allclose(advantages, expected-values, atol=1e-7)
    np.testing.assert_allclose(advantages[:, 0], -advantages[:, 1])


def test_private_paired_critic_is_antisymmetric_and_starts_zero():
    critic = PairedCritic()
    obs = torch.randn(3, 3256)
    assert not critic(obs).any()
    torch.nn.init.normal_(critic.score[-1].weight, std=.02)
    value = critic(obs)
    swapped = obs.clone()
    swapped[:, 3000:3128], swapped[:, 3128:] = obs[:, 3128:], obs[:, 3000:3128]
    torch.testing.assert_close(critic(swapped), -value)
    obs[:, :3000] += 1000
    assert torch.equal(critic(obs), value)


def test_ppo_launcher_resolves_selected_transformer_and_conservative_recipe():
    result = subprocess.run([sys.executable, "ocean/kaggriculture/run.py", "transformer-ppo", "--dry-run"],
                            cwd=ROOT, text=True, capture_output=True, check=True)
    plan = json.loads(result.stdout)
    assert plan["actor_checkpoint"].endswith("optimized_20261007/epoch_19.pt")
    assert plan["actor_features"] == 3000 and plan["private_critic_features"] == 256
    assert plan["fresh_resets"] and plan["ppo"]["gamma"] == 1
    assert plan["ppo"]["actor_learning_rate"] == 3e-6
    assert plan["ppo"]["teacher_kl_coef"] > 0
    assert plan["ppo"]["critic_warmup_rollouts"] > 0
    assert not Path(plan["output"]).exists()


def test_default_transformer_eval_uses_pinned_actor_and_validation_cohort():
    result = subprocess.run([sys.executable, "ocean/kaggriculture/run.py", "transformer-eval", "--dry-run"],
                            cwd=ROOT, text=True, capture_output=True, check=True)
    plan = json.loads(result.stdout)
    assert plan["checkpoint"].endswith("optimized_20261007/epoch_19.pt")
    assert plan["eval_games"] == 64 and plan["map_seed_mode"] == "explicit"
    assert plan["eval_seed"] == 2026100700
    assert not Path(plan["output"]).exists()


def test_complete_native_selfplay_rollout_preserves_sampled_likelihoods():
    if not (ROOT / "ocean/kaggriculture_direct/build/replay.so").exists():
        pytest.skip("requires replay bridge")

    class UniformActor(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.weight = torch.nn.Parameter(torch.zeros(1))
            self.grad_modes = []

        def forward(self, obs):
            self.grad_modes.append(torch.is_grad_enabled())
            return obs.new_zeros(len(obs), LOGITS) + self.weight*0

    class ZeroCritic(torch.nn.Module):
        def forward(self, obs):
            return obs.new_zeros(len(obs))

    actor, teacher = UniformActor(), UniformActor()
    threads = torch.get_num_threads()
    torch.set_num_threads(1)
    try:
        rollout, metrics = collect_rollout(selfplay_bridge(), actor, teacher, ZeroCritic(),
            actor, teacher, ActionDistribution(), PPOConfig(), [2026101900], 240)
    finally:
        torch.set_num_threads(threads)
    assert metrics["games"] == 1 and metrics["agent_steps"] == 1438
    assert all(actor.grad_modes) and not any(teacher.grad_modes)
    assert rollout["obs"].shape == (1438, 3256)
    expected = np.zeros(1438)
    offset = 0
    for h, size in enumerate(SIZES):
        support = rollout["masks"][:, offset:offset+size]
        assert support[np.arange(1438), rollout["actions"][:, h]].all()
        expected -= np.log(support.sum(-1))
        offset += size
    np.testing.assert_allclose(rollout["old_logp"], expected, rtol=1e-6, atol=1e-5)
    assert np.array_equal(rollout["mc_returns"][0::2], -rollout["mc_returns"][1::2])
