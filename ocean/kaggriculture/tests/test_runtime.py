"""Opt-in full rollout/update parity, including CPU workers and heterogeneous banks."""

import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.parametrize("backend,buffers", [("GPU", 1), ("CPU", 1), ("CPU", 2)])
@pytest.mark.parametrize("agents,policies", [(1, 1), (2, 1), (2, 3)])
def test_graphs_slots_and_cpu_workers_preserve_training(tmp_path, backend, buffers, agents, policies):
    name = os.environ.get(f"KAGGRICULTURE_{backend}_RUNTIME_BINARY")
    if name is None:
        pytest.skip(f"set KAGGRICULTURE_{backend}_RUNTIME_BINARY on an idle GPU")
    binary = Path(name).resolve()
    assert binary.is_file()
    options = {
        "base.load_model_path": "None", "base.seed": 73,
        "selfplay.enabled": 0, "selfplay.initial_opponents": "None",
        "vec.total_agents": 64, "vec.num_buffers": buffers, "vec.num_threads": 4,
        "vec.num_policies": policies, "vec.hist_policy_percent": 0.5,
        "vec.hist_policy_hidden_size": 64, "vec.hist_policy_num_layers": 2,
        "policy.hidden_size": 32, "policy.num_layers": 1,
        "env.num_agents": agents, "env.learner_seat": 1, "env.reset_state_prob": 0,
        "train.horizon": 16, "train.minibatch_size": 64, "train.replay_ratio": 1,
        "train.learning_rate": 0.001, "train.anneal_lr": 0, "train.anneal_ent_coef": 0,
        "train.total_timesteps": 4096,
    }
    reference = None
    for async_mode, graphs in [(0, -1), (0, 1), (1, -1), (1, 1)]:
        output = tmp_path / f"a{async_mode}_g{graphs}.bin"
        settings = dict(options, **{"base.async": async_mode, "base.cudagraphs": graphs})
        result = subprocess.run([str(binary), "dump", str(output)] +
            [f"--{key}={value}" for key, value in settings.items()],
            cwd=ROOT, text=True, capture_output=True, timeout=180)
        assert result.returncode == 0, result.stdout + result.stderr
        actual = output.read_bytes()
        if reference is None:
            reference = actual
        assert actual == reference, (backend, buffers, agents, policies, async_mode, graphs)
