"""Opt-in balanced-panel tests; requires a local ROM and a compatible checkpoint.

RETRO_PANEL_BINARY=build/retro/sweep_eval RETRO_PANEL_CHECKPOINT=PATH.bin \
    uv run --no-project --with pytest python -m pytest -q ocean/retro/tests/test_panel.py
"""

import collections
import configparser
import csv
import io
import os
from pathlib import Path
import re
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture(scope="module")
def panel(tmp_path_factory):
    executable = os.environ.get("RETRO_PANEL_BINARY")
    checkpoint = os.environ.get("RETRO_PANEL_CHECKPOINT")
    if not executable or not checkpoint:
        pytest.skip("set RETRO_PANEL_BINARY and RETRO_PANEL_CHECKPOINT")
    directory = tmp_path_factory.mktemp("retro_panel")
    args = [str(Path(executable).resolve()), str(Path(checkpoint).resolve()),
        "--config", str(ROOT / "config/retro.ini"), "--full-run", "--levels", "all",
        "--metric", "checkpoints", "--frames", "512", "--repeats", "2", "--seed", "17"]
    report = directory / "baseline.tsv"
    subprocess.run(args + ["--workers", "1", "--output", str(report)],
        cwd=ROOT, check=True, capture_output=True, text=True, timeout=180)
    return args, report, directory


def test_equal_level_counts_and_terminal_progress(panel):
    _, report, _ = panel
    text = report.read_text()
    rows = list(csv.DictReader(io.StringIO("\n".join(
        line for line in text.splitlines() if not line.startswith("#"))), delimiter="\t"))
    assert len(rows) == 64
    assert collections.Counter(row["level"] for row in rows) == {
        f"{world}-{stage}": 2 for world in range(1, 9) for stage in range(1, 5)}
    assert all(int(row["frames"]) <= 512 for row in rows)
    # These episodes truncate and auto-reset. Preserve pre-reset progress, not zero.
    assert any(float(row["checkpoints"]) > 0 for row in rows)
    mean = sum(float(row["checkpoints"]) for row in rows) / len(rows)
    assert float(re.search(r"# score=([^ ]+)", text)[1]) == pytest.approx(mean)


def test_panel_is_independent_of_worker_schedule(panel):
    args, report, directory = panel
    other = directory / "parallel.tsv"
    subprocess.run(args + ["--workers", "4", "--output", str(other)],
        cwd=ROOT, check=True, capture_output=True, text=True, timeout=180)
    assert other.read_bytes() == report.read_bytes()


def test_score_is_independent_of_training_rewards_and_layout(panel):
    args, report, directory = panel
    config = configparser.ConfigParser()
    config.read(ROOT / "config/retro.ini")
    config["env"].update({"death_penalty": "5", "checkpoint_distance": "16",
        "checkpoint_reward": "50", "novel_area_reward": "9", "spawn_levels": "2-2",
        "max_frames": "100", "terminate_on_clear": "0"})
    config["vec"].update({"total_agents": "4096", "num_buffers": "8"})
    config["train"].update({"horizon": "2048", "minibatch_size": "65536"})
    alternate = directory / "alternate.ini"
    with alternate.open("w") as file:
        config.write(file)
    other = directory / "alternate.tsv"
    subprocess.run(args + ["--config", str(alternate), "--workers", "4", "--output", str(other)],
        cwd=ROOT, check=True, capture_output=True, text=True, timeout=180)
    assert other.read_bytes() == report.read_bytes()
