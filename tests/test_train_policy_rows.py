"""Explicitly opt-in native trainer data-selection tests; never builds implicitly."""

import os
from pathlib import Path
import subprocess

import pytest


@pytest.mark.parametrize("policies", [1, 2])
@pytest.mark.parametrize("buffers", [1, 2])
@pytest.mark.parametrize("async_mode", [0, 1])
@pytest.mark.parametrize("graphs", [0, 1])
def test_only_learner_rows_enter_ppo(policies, buffers, async_mode, graphs):
    binary = os.environ.get("PUFFER_POLICY_ROWS_TEST_BINARY")
    if binary is None:
        pytest.skip("set PUFFER_POLICY_ROWS_TEST_BINARY on an idle GPU")
    result = subprocess.run([str(Path(binary).resolve()), str(policies), str(buffers),
        str(async_mode), str(graphs)], cwd=Path(__file__).resolve().parents[1],
        capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "policy row selection PASS" in result.stdout
