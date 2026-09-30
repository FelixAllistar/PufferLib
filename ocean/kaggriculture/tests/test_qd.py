import asyncio
import configparser
import importlib.util
import json
from pathlib import Path
import pickle
from types import SimpleNamespace

import numpy as np
import pytest

SPEC = importlib.util.spec_from_file_location("qd", Path(__file__).parents[1] / "qd.py")
qd = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qd)


def manifest():
    return dict(config={"base.load_model_path": "/fixed/bc.bin", "train.learning_rate": ".001",
        "train.gae_lambda": ".99965", "env.reward_money": "1", "env.reward_growth_land": "0",
        "env.potential_beta": "0", "env.reset_state_prob": ".7"},
        ranges=[[0, .2], [0, .5], [0, .2], [0, 2], [0, 1]],
        settings=dict(archive_bins=[8, 8], seed=42, archive_alpha=.1, sigma=.25,
            batch_size=10, eval_games=64, eval_seed=3))


def test_only_rewards_change_and_bc_never_changes():
    m = manifest()
    a = qd.candidate_config(m, 0, [0] * 5)
    b = qd.candidate_config(m, 1, [1] * 5)
    changed = {key for key in a if a[key] != b[key]}
    assert changed == {"base.run_id", *("env." + key for key in qd.REWARDS)}
    assert a["base.load_model_path"] == b["base.load_model_path"] == "/fixed/bc.bin"
    assert a["train.learning_rate"] == b["train.learning_rate"] == ".001"
    assert a["env.reset_state_prob"] == b["env.reset_state_prob"] == ".7"


def test_eval_panel_overrides_resets_and_shaping():
    m = manifest()
    c = qd.evaluation_config(m, Path("champ.bin"), "opponent.bin", 456)
    assert c["base.load_model_path"] == "champ.bin"
    assert c["env.reset_state_prob"] == "0"
    assert c["base.eval_agents"] == "128" and c["base.eval_episodes"] == "64"
    assert c["selfplay.enabled"] == "0" and c["env.num_agents"] == "2"
    assert c["base.seed"] == "456"
    assert all(c["env." + key] == "0" for key in qd.REWARDS)


def test_dashboard_parser_and_two_line_fields():
    d = qd.Dashboard()
    lines = ["╭────╮", "│ Steps       12.4M      Env     10ms   5%  value   2.0 │",
        "│ SPS          94.3K     Copy     0ms   0%  entropy 0.12 │",
        "│ root_money        80123.500   reset_money        90000.000 │",
        "│ plants               98.400   animal_places         11.500 │",
        "│ ending_plots          2.014   reset_fraction          0.500 │", "╰────╯"]
    for line in lines:
        frame = d.feed("\033[0m" + line)
    assert frame["steps"] == 12400000 and frame["sps"] == 94300
    assert frame["root_money"] == 80123.5 and frame["animal_places"] == 11.5
    line = qd.status_line("1", dict(trial="#2", phase="train", metrics=frame))
    assert "reset$=90000" in line and "plants=98.4" in line and "animals=11.5" in line
    d.feed("╭────╮")
    d.feed("│ Steps        12.6M │")
    frame = d.feed("╰────╯")
    assert frame["root_money"] == 80123.5  # no terminal games in the latest dashboard
    frame["reset_fraction"] = 0
    assert "reset$=off" in qd.status_line("1", dict(metrics=frame))
    d.feed('KAG_QD_METRICS {"animal_delay": 0.123456789, "root_money": 123456.789}')
    d.feed("╭────╮")
    d.feed("│ root_money 123456.78 │")
    exact = d.feed("╰────╯")
    assert exact["root_money"] == 123456.789 and exact["animal_delay"] == .123456789


def test_measurements_are_observed_behavior_not_training_reward():
    results = []
    for value in (100, 300):
        metrics = dict(root_money=50000, crop_ref_value=100, animal_ref_value=value,
            animal_delay=.25, animal_seen=1, plot2_delay=.4, plot3_delay=1,
            plants=50, animal_places=8, ending_plots=2, episode_return=999999)
        results.append(dict(games=64, win_rate=.5, metrics=metrics))
    result = qd.measure_results(results)
    assert result["objective"] == 50000
    assert result["measures"] == [2/3, .25]
    del results[0]["metrics"]["animal_delay"]
    with pytest.raises(AssertionError, match="rebuild"):
        qd.measure_results(results)


def batch(state, objectives, ok=None):
    solutions = state["scheduler"].ask().tolist()
    state["pending"] = dict(solutions=solutions)
    results = [dict(trial=state["next_trial"] + i, ok=ok[i] if ok else True,
        objective=v, measures=[.5, .5]) for i, v in enumerate(objectives)]
    qd.finish_batch(state, results)


def test_mae_covariance_updates_result_archive_never_loses_best():
    state = qd.new_search(manifest())
    batch(state, list(range(100, 110)))
    assert state["result_archive"].stats.obj_max == 109
    batch(state, [90] * 10)
    assert state["result_archive"].stats.obj_max == 109
    batch(state, [999] * 10, ok=[False] * 10)
    assert state["result_archive"].stats.obj_max == 109
    assert state["next_trial"] == 30


def test_resume_preserves_pending_candidates_and_rng(tmp_path):
    state = qd.new_search(manifest())
    state["pending"] = dict(solutions=state["scheduler"].ask().tolist())
    qd.save_search(tmp_path, state)
    with (tmp_path / "state.pkl").open("rb") as stream:
        restored = pickle.load(stream)
    assert restored["pending"] == state["pending"]
    results = [dict(trial=i, ok=True, objective=100+i, measures=[i/10, .4]) for i in range(10)]
    qd.finish_batch(state, results)
    qd.finish_batch(restored, results)
    np.testing.assert_array_equal(state["scheduler"].ask(), restored["scheduler"].ask())


def test_prepare_snapshots_and_rejects_non_bc(tmp_path):
    repo = qd.ROOT
    config = qd.read_config(repo / "config/default.ini")
    config.update(qd.read_config(repo / "config/kaggriculture.ini"))
    config.update({"env.reset_state_prob": "0", "vec.num_policies": "2",
        "selfplay.enabled": "1", "selfplay.opp_timeout_steps": "0"})
    qd.write_config(tmp_path / "input.ini", config)
    bc = repo / "saved/kaggriculture/initial_bc.bin"
    if not bc.exists():
        pytest.skip("local actor-only BC artifact is not installed")
    opponents = tmp_path / "opponents.txt"
    opponents.write_text(str(bc) + "\n")
    args = SimpleNamespace(config=tmp_path / "input.ini", bc=bc,
        binary=Path("/bin/true"), opponents=opponents,
        settings=Path(__file__).parents[1] / "profiles/qd.ini", steps=10000000,
        output=tmp_path / "prepared")
    qd.prepare(args)
    saved = json.loads((args.output / "manifest.json").read_text())
    assert saved["bc_metadata"]["terminal_critic"] is False
    assert saved["config"]["base.load_model_path"] == str(args.output / "bc.bin")
    assert saved["config"]["train.learning_rate"] == config["train.learning_rate"]
    assert not (args.output / "state.pkl").exists()
    assert not list(args.output.glob("trial_*"))
    args.output = tmp_path / "bad"
    args.bc = repo / "saved/kaggriculture/initial_bc_critic.bin"
    if args.bc.exists():
        with pytest.raises(AssertionError, match="actor-only"):
            qd.prepare(args)


def test_two_worker_pipeline_failure_isolation_and_resume(tmp_path, capsys):
    m = manifest()
    m["settings"].update(max_trials=10, display_seconds=1)
    m["binary"] = str(Path(__file__).with_name("qd_fake_worker.py"))
    m["opponents"] = ["frozen1.bin", "frozen2.bin"]
    m["config"].update({"base.checkpoint_dir": str(tmp_path / "checkpoints"),
        "base.load_model_path": str(tmp_path / "bc.bin"), "vec.total_agents": "8",
        "train.horizon": "8", "train.total_timesteps": "128", "base.gpu_offset": "0",
        "train.gpus": "1", "base.result_fd": "0"})
    (tmp_path / "bc.bin").write_text("same actor-only BC")
    asyncio.run(qd.run_search(tmp_path, m, ["3", "7"]))
    status = json.loads((tmp_path / "status.json").read_text())
    assert status["completed"] == 10 and status["last_batch_failed"] == 1
    elites = json.loads((tmp_path / "elites.json").read_text())
    assert all(elite["trial"] != 2 and elite["objective"] == 12000 for elite in elites)
    devices = [json.loads(p.read_text()) for p in (tmp_path / "checkpoints").rglob("device.json")]
    assert {d["device"] for d in devices} == {"3", "7"}
    assert all(d["source"] == str(tmp_path / "bc.bin") for d in devices)
    assert len(devices) == 9
    before = (tmp_path / "trial_0000/result.json").stat().st_mtime_ns
    asyncio.run(qd.run_search(tmp_path, m, ["3", "7"]))
    assert (tmp_path / "trial_0000/result.json").stat().st_mtime_ns == before
    output = capsys.readouterr().out
    assert "GPU3" in output and "GPU7" in output and "root$=" in output
