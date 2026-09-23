import importlib.util
from pathlib import Path

import numpy as np
import pytest


spec = importlib.util.spec_from_file_location("kag_run", Path(__file__).parents[1] / "run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def test_nash_rock_paper_scissors():
    league = {"models": {"rock": {}, "paper": {}, "scissors": {}}, "matches": {
        "rock/paper": {"score": 0}, "rock/scissors": {"score": 1},
        "paper/scissors": {"score": 0}}}
    strategy = runner.solve_league(league)
    np.testing.assert_allclose(list(strategy.values()), [1 / 3] * 3)


def test_dominant_checkpoint_and_tie_keeps_champion():
    league = {"models": {"old": {}, "new": {}}, "matches": {"old/new": {"score": 0.1}}}
    assert runner.solve_league(league) == {"old": 0, "new": 1}
    assert league["champion"] == "new"
    league["matches"]["old/new"]["score"] = 0.5
    runner.solve_league(league)
    assert league["champion"] == "new"


def test_incomplete_matrix_cannot_sample_fake_strategy():
    with pytest.raises(KeyError):
        runner.solve_league({"models": {"a": {}, "b": {}}, "matches": {}})


def test_atomic_league_roundtrip(tmp_path):
    path = tmp_path / "league.json"
    league = {"contract": runner.CONTRACT, "models": {}, "matches": {}}
    runner.save_league(path, league)
    assert runner.json.loads(path.read_text()) == league
    assert not path.with_suffix(".tmp").exists()
