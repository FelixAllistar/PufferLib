import configparser
import importlib.util
import json
import os
from pathlib import Path
import struct

import pytest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("kag_sweep_worker", ROOT/"ocean/kaggriculture_direct/sweep_worker.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


def settings():
    ini = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=("#", ";"))
    ini.read([ROOT/"config/default.ini", ROOT/"config/kaggriculture.ini"])
    return ini


def test_sweep_only_varies_four_optimizer_settings():
    ini = settings()
    varied = set()
    for section in ini.sections():
        if not section.startswith("sweep."):
            continue
        target, key = section[6:].rsplit(".", 1)
        lo, hi = ini.getfloat(section, "min"), ini.getfloat(section, "max")
        value = (ini.getfloat("sweep", "trial_timesteps") if (target, key) == ("train", "total_timesteps")
                 else ini.getfloat(target, key))
        assert lo <= value <= hi, section
        if lo != hi:
            varied.add(f"{target}.{key}")
    assert varied == {"train.learning_rate", "train.ent_coef", "train.vf_coef", "train.momentum"}
    assert ini.getint("sweep", "gpus") == ini.getint("train", "gpus") == 1
    assert ini.getint("sweep", "downsample") == 1
    assert ini.getint("sweep", "trial_timesteps") < ini.getint("train", "total_timesteps")
    assert ini.get("base", "load_model_path").endswith("h1024_l2/bc.bin")
    assert ini.getint("policy", "hidden_size") == 1024
    assert ini.getfloat("train", "teacher_kl_coefficient") == 0
    assert ini.get("sweep", "metric") == "root_money"


def test_eval_is_fresh_fixed_opponent_and_seat_balanced():
    values = {"sweep.eval_seed": "400", "sweep.eval_agents": "32", "sweep.eval_games_per_seat": "128"}
    for seat in (0, 1):
        over = worker.eval_overrides(values, "trained.bin", seat)
        assert over["env.reset_state_prob"] == "0"
        assert over["env.reset_state_bank"] == "None"
        assert over["env.learner_seat"] == str(seat)
        assert over["env.bot_policy"] == "1" and over["env.num_agents"] == "1"
        assert over["selfplay.enabled"] == "0" and over["vec.num_policies"] == "1"
        assert over["train.teacher_kl_coefficient"] == "0"
        assert over["base.seed"] == "400"
        cmd = worker.command("puffer", "eval", {"base.result_fd": "99"}, over)
        assert "--base.result_fd=0" in cmd and "--headless" in cmd
        assert "--base.result_fd=99" not in cmd


def test_train_result_native_wire_layout():
    packet = worker.result_packet(1234, 56, 368640)
    assert len(packet) == 784
    fields = struct.unpack("=fffi192f", packet)
    assert fields[:4] == (1234, 56, 368640, 1)
    assert fields[4] == 1234 and fields[68] == 56 and fields[132] == 368640
    assert sum(fields[5:68]) == sum(fields[69:132]) == sum(fields[133:]) == 0


@pytest.mark.parametrize("text", ["", "CUDA_EVAL env=kaggriculture score=nan perf=.5 games=128 params=10 draw=0",
                                  "CUDA_EVAL env=kaggriculture score=3 perf=.5 games=5 params=10 draw=0"])
def test_failed_eval_is_not_a_good_score(text):
    with pytest.raises(ValueError):
        worker.parse_eval(text, 128)


@pytest.mark.parametrize("fail_eval", [False, True])
def test_worker_train_then_two_eval_processes_and_preserved_logs(tmp_path, monkeypatch, fail_eval):
    ini = settings()
    values = {f"{s}.{k}": v for s in ini.sections() for k, v in ini[s].items()}
    values.update({"base.run_id": "sweep_1234_0000", "train.total_timesteps": "368640"})
    initial = tmp_path/values["base.load_model_path"]
    initial.parent.mkdir(parents=True)
    initial.write_bytes(b"checkpoint")
    read_fd, write_fd = os.pipe()
    values["base.result_fd"] = str(write_fd)
    monkeypatch.setattr(worker, "ROOT", tmp_path)
    calls = []

    def fake_run(cmd, path):
        calls.append(cmd[1])
        if cmd[1] == "train":
            ckpt = tmp_path/"checkpoints/kaggriculture/sweep_1234_0000/0000000000368640.bin"
            ckpt.parent.mkdir(parents=True)
            ckpt.write_bytes(initial.read_bytes())
            path.write_text("training finished\n")
        else:
            assert calls[0] == "train"
            if fail_eval:
                path.write_text("evaluation failed\n")
                raise ValueError("eval failed")
            seat = int(next(x.split("=", 1)[1] for x in cmd if x.startswith("--env.learner_seat=")))
            path.write_text(f"CUDA_EVAL env=kaggriculture score={3000 + 200*seat} perf=.5 games=128 params=10 draw=0\n")

    monkeypatch.setattr(worker, "run_logged", fake_run)
    try:
        if fail_eval:
            with pytest.raises(ValueError, match="eval failed"):
                worker.run_trial(values)
            assert os.read(read_fd, 784) == b""  # Never returns the training metric on failure.
        else:
            worker.run_trial(values)
            assert calls == ["train", "eval", "eval"]
            packet = struct.unpack("=fffi192f", os.read(read_fd, 784))
            assert packet[0] == 3100 and packet[3] == 1
            result = json.loads((tmp_path/"logs/kaggriculture/sweep_1234_0000/result.json").read_text())
            assert result["status"] == "complete" and len(result["evaluations"]) == 2
    finally:
        os.close(read_fd)
