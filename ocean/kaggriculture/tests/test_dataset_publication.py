import configparser
import gzip
import json
from pathlib import Path
import struct
import subprocess
import sys

import numpy as np
import pytest

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import build_entity_bc_dataset as dataset
import run as runner


def test_native_dataset_publication(tmp_path):
    library = tmp_path / "bridge.so"
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-Isrc",
        "-Iraylib-5.5_linux_amd64/include", "-DKAG_BC_SOURCE_HASH=123ULL",
        str(HERE / "kag_bc_replay.c"), "-lm", "-o", str(library)], cwd=ROOT, check=True)
    entries = []
    for episode, split in [("fixture1", "train"), ("fixture2", "holdout")]:
        tape = dict(configuration={"episodeSteps": 720, "seed": 123},
            info={"EpisodeId": episode, "TeamNames": ["teacher", "opponent"], "seed": 123},
            frames=720, parity_frames=720, rewards=[3000, 3000],
            source_sha256="synthetic-native-pass-fixture",
            actions=[[{}, {}] for _ in range(719)])
        path = tmp_path / f"{episode}.json.gz"
        with gzip.open(path, "wt") as stream:
            json.dump(tape, stream)
        entries.append(dict(episode_id=episode, split=split, path=str(path)))
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({"cache": entries}))
    output = tmp_path / "test.bc"
    command = [sys.executable, str(HERE / "build_entity_bc_dataset.py"),
        "--manifest", str(manifest), "--lib", str(library), "--teacher", "teacher",
        "--profile", str(HERE / "profiles/terminal.ini"), "--output", str(output)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    header = dataset.HEADER.unpack(output.read_bytes()[:88])
    assert header[6:8] == (2, 720) and header[15] == 1
    assert header[16] == 123
    raw = output.read_bytes()
    count, obs, heads, packed = header[2:6]
    offset = 88 + count * obs * 4
    actions = np.frombuffer(raw, "<f4", count * heads, offset).reshape(count, heads)
    offset += count * heads * 4
    masks = np.unpackbits(np.frombuffer(raw, "u1", count * packed, offset)
        .reshape(count, packed), axis=1, bitorder="little")
    offset += count * packed
    targets = np.frombuffer(raw, "<f4", count, offset)
    assert len(raw) == offset + count * 4
    assert np.isnan(targets[[719, 1439]]).all()
    assert np.all(actions[[719, 1439]] == -1)
    assert np.all(targets[np.isfinite(targets)] == 0)
    sizes = [44] * 17 + [2, 21, 100] * 10
    begin = 0
    for head, size in enumerate(sizes):
        rows = np.where(actions[:, head] >= 0)[0]
        values = actions[rows, head].astype(int)
        assert np.all(values < size)
        assert np.all(masks[rows, begin + values] == 1)
        begin += size
    config = configparser.ConfigParser(interpolation=None)
    config.read([ROOT / "config/default.ini", ROOT / "config/kaggriculture.ini",
        HERE / "profiles/terminal.ini"])
    assert runner.validate_dataset(output, config, "joint")["validation_games"] == 1
    config.set("env", "reward_money", "100")
    with pytest.raises(AssertionError, match="reward mismatch"):
        runner.validate_dataset(output, config, "joint")
    # Actor-only BC does not consume expert returns or their discount factor.
    config.set("train", "gamma", "0.5")
    runner.validate_dataset(output, config, "bc")
    with pytest.raises(AssertionError, match="gamma mismatch"):
        runner.validate_dataset(output, config, "joint")
    config.set("env", "market_slots", "1")
    with pytest.raises(AssertionError, match="controller mismatch"):
        runner.validate_dataset(output, config, "bc")
    before = output.read_bytes()
    assert subprocess.run(command, cwd=ROOT, capture_output=True).returncode != 0
    assert output.read_bytes() == before
