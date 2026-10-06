import argparse
import configparser
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("direct_run", ROOT/"ocean/kaggriculture_direct/run.py")
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


def checkpoint(path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"weights!" * 4)
    receipt = dict(expected, parameters=8, sha256=run.digest(path), selected_epoch=30, heldout_loss=.67)
    path.with_suffix(".json").write_text(json.dumps(receipt))
    return receipt


@pytest.fixture
def expected():
    return dict(policy=6, observation=4, macro=0, executor=0, hidden=512, layers=2,
                config="one config", default_config="shared defaults", dataset_sha256="dataset",
                trainer_sha256="trainer", command=["bc", "--policy.hidden_size=512"], mode="actor")


def test_completed_only_skipped_with_explicit_resume(tmp_path, expected):
    path = tmp_path/"bc.bin"
    assert not run.completed_checkpoint(path, expected, resume=True)
    checkpoint(path, expected)
    before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in tmp_path.iterdir()}
    with pytest.raises(FileExistsError, match="--resume"):
        run.completed_checkpoint(path, expected)
    assert run.completed_checkpoint(path, expected, resume=True)
    assert before == {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in tmp_path.iterdir()}


@pytest.mark.parametrize("field", ["policy", "observation", "macro", "executor", "hidden", "layers",
                                   "config", "default_config", "dataset_sha256", "trainer_sha256", "command", "mode"])
def test_resume_rejects_changed_recipe(tmp_path, expected, field):
    path = tmp_path/"bc.bin"
    checkpoint(path, expected)
    with pytest.raises(ValueError, match=f"mismatch for {field}"):
        run.completed_checkpoint(path, dict(expected, **{field: "changed"}), resume=True)


@pytest.mark.parametrize("missing", ["bc.bin", "bc.json"])
def test_resume_rejects_incomplete_pair(tmp_path, expected, missing):
    path = tmp_path/"bc.bin"
    checkpoint(path, expected)
    (tmp_path/missing).unlink()
    with pytest.raises(ValueError, match="incomplete checkpoint/receipt pair"):
        run.completed_checkpoint(path, expected, resume=True)


def test_resume_rejects_same_size_corruption(tmp_path, expected):
    path = tmp_path/"bc.bin"
    checkpoint(path, expected)
    path.write_bytes(b"corrupt!" * 4)
    with pytest.raises(ValueError, match="checksum mismatch"):
        run.completed_checkpoint(path, expected, resume=True)


@pytest.mark.parametrize("changes,reason", [({"parameters": 1}, "size"),
                                          ({"selected_epoch": None}, "selection"),
                                          ({"heldout_loss": float("nan")}, "loss")])
def test_resume_rejects_invalid_receipt(tmp_path, expected, changes, reason):
    path = tmp_path/"bc.bin"
    receipt = checkpoint(path, expected)
    path.with_suffix(".json").write_text(json.dumps(dict(receipt, **changes)))
    with pytest.raises(ValueError, match=reason):
        run.completed_checkpoint(path, expected, resume=True)


def test_original_receipt_without_default_snapshot_is_supported(tmp_path, expected):
    path = tmp_path/"bc.bin"
    original = dict(expected)
    del original["default_config"]
    checkpoint(path, original)
    assert run.completed_checkpoint(path, expected, resume=True)


def test_queue_validates_all_shapes_then_runs_only_missing(tmp_path, monkeypatch):
    monkeypatch.setattr(run, "ROOT", tmp_path)
    config = tmp_path/"config"
    config.mkdir()
    (config/"kaggriculture.ini").write_text("main config")
    (config/"default.ini").write_text("defaults")
    binary = tmp_path/"trainer"
    binary.write_bytes(b"same trainer")
    ini = configparser.ConfigParser()
    ini.read_dict({"bc": {"data": "teacher.bc"}, "bc_grid": {"output_root": "saved"}})
    monkeypatch.setattr(run, "check_dataset", lambda path: {"sha256": "dataset"})
    args = argparse.Namespace(mode="bc-grid", bc_binary=binary, dry_run=False, resume=True)
    shapes = [(h, l) for h in (256, 512, 1024) for l in (2, 3)]
    common = dict(policy=6, observation=4, macro=0, executor=0, optimizer="Adam", config="main config",
                  default_config="defaults", dataset_sha256="dataset", trainer_sha256=run.digest(binary))
    for h, l in shapes[:3]:
        output = run.model_path(ini, h, l)
        command = [str(binary), f"--policy.hidden_size={h}", f"--policy.num_layers={l}",
                   "--bc.mode=actor", f"--bc.data={tmp_path/'teacher.bc'}", "--base.load_model_path=None",
                   f"--bc.output={output}"]
        checkpoint(output, dict(common, hidden=h, layers=l, mode="actor", command=command))
    kept = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in (tmp_path/"saved").glob("*/*")}
    started = []

    def fake_launch(command, dry_run, log, preflight):
        assert preflight and not dry_run
        started.append(command)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text("BC selected_epoch=30 heldout_loss=0.6\n")
        output = Path(next(x.split("=", 1)[1] for x in command if x.startswith("--bc.output=")))
        output.write_bytes(b"weights!" * 4)

    monkeypatch.setattr(run, "launch", fake_launch)
    broken = run.model_path(ini, 1024, 3).with_suffix(".json")
    broken.parent.mkdir(parents=True)
    broken.write_text("{}")
    with pytest.raises(ValueError, match="incomplete"):
        run.run_bc(args, ini, shapes, [])
    assert not started
    broken.unlink()
    run.run_bc(args, ini, shapes, [])
    assert len(started) == 3
    assert "--policy.hidden_size=512" in started[0] and "--policy.num_layers=3" in started[0]
    assert all((p.read_bytes(), p.stat().st_mtime_ns) == before for p, before in kept.items())
    assert len(list((tmp_path/"saved").glob("*/bc.json"))) == 6
    run.run_bc(args, ini, shapes, [])
    assert len(started) == 3  # Fully completed queue never even launches a CUDA check.


def test_failed_preflight_is_logged_and_never_launches_bc(tmp_path, monkeypatch):
    # This fake check executes in its own process, as the real CUDA probe does.
    monkeypatch.setattr(run, "ROOT", tmp_path)
    probe = tmp_path/"ocean/kaggriculture_direct/check_cuda.py"
    probe.parent.mkdir(parents=True)
    probe.write_text("print('CUDA preflight failed: cudaErrorUnknown (999)', flush=True)\nraise SystemExit(1)\n")
    sentinel = tmp_path/"trainer_started"
    command = [sys.executable, "-c", "from pathlib import Path; Path('trainer_started').touch()"]
    log = tmp_path/"bc.log"
    with pytest.raises(subprocess.CalledProcessError):
        run.launch(command, False, log, preflight=True)
    assert not sentinel.exists()
    assert "cudaErrorUnknown (999)" in log.read_text()


def test_resume_not_silently_accepted_for_ppo():
    p = subprocess.run([sys.executable, "ocean/kaggriculture/run.py", "train", "--resume", "--dry-run"],
                       cwd=ROOT, text=True, capture_output=True)
    assert p.returncode != 0 and "only supported for bc" in p.stderr
