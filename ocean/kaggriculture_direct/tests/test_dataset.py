"""Offline tests only. Opt-in fixture cache is already on disk; no downloads."""
import gzip
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import pytest

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
spec = importlib.util.spec_from_file_location("direct_dataset", HERE / "build_bc_dataset.py")
dataset = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dataset)


def cache_records():
    manifest = ROOT / "data/kaggriculture/terminal.json"
    if not manifest.exists():
        pytest.skip("requires local parity-checked replay cache")
    data = json.loads(manifest.read_text())
    entries = data.get("cache", data.get("records", []))
    chosen = []
    for split in ("train", "holdout"):
        record = next(e for e in entries if e["split"] == split)
        record = dict(record, path=record.get("path", record.get("tape")))
        chosen.append(record)
    return {"cache": chosen}


def test_profile_contract(tmp_path):
    dataset.validate_profile(ROOT / "config/kaggriculture.ini")
    text = (ROOT / "config/kaggriculture.ini").read_text()
    path = tmp_path / "shaped.ini"
    path.write_text(text.replace("reward_growth_crop = 0", "reward_growth_crop = 1"))
    with pytest.raises(ValueError, match="unshaped"):
        dataset.validate_profile(path)


def test_episode_split_isolation():
    manifest = {"cache": [dict(episode_id=1, split="train", path="unused"),
                           dict(episode_id=1, split="holdout", path="unused")]}
    with pytest.raises(ValueError, match="split leakage"):
        dataset.select_entries(manifest, [])


def test_real_tapes_roundtrip(tmp_path):
    lib = HERE / "build/replay.so"
    if not lib.exists():
        pytest.skip("make -C ocean/kaggriculture_direct replay-bridge first")
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps(cache_records()))
    output = tmp_path / "teacher.bc"
    command = [sys.executable, str(HERE / "build_bc_dataset.py"), "--manifest", str(manifest),
               "--tape-root", str(ROOT / "data/kaggriculture/tapes"), "--output", str(output)]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    fields = dataset.HEADER.unpack(output.read_bytes()[:dataset.HEADER.size])
    assert fields[:8] == (0x4b414742, 3, 2880, dataset.OBS, dataset.HEADS, dataset.PACKED, 4, 720)
    assert fields[8:16] == (4,5,7,0,0,1,0,2)
    assert fields[-1] == 1
    metadata = json.loads(output.with_suffix(".json").read_text())
    assert metadata["train_games"] == metadata["validation_games"] == 2
    count = fields[2]
    offset = dataset.HEADER.size + count*dataset.OBS*4
    labels = np.memmap(output,mode="r",dtype=np.float32,shape=(count,dataset.HEADS),offset=offset)
    offset += labels.nbytes
    masks = np.memmap(output,mode="r",dtype=np.uint8,shape=(count,dataset.PACKED),offset=offset)
    offset += masks.nbytes
    returns = np.memmap(output,mode="r",dtype=np.float32,shape=(count,),offset=offset)
    assert np.isfinite(labels).all()
    offsets = np.r_[0,np.cumsum(dataset.SIZES)[:-1]]
    for row in range(count):
        support = np.unpackbits(masks[row],bitorder="little")[:dataset.LOGITS]
        for head, start in enumerate(offsets):
            width = dataset.SIZES[head]
            action = int(labels[row,head])
            if support[start:start+width].sum() == 1:
                assert action == -1, "forced or inactive label must be excluded"
            if action >= 0:
                assert action < width and support[start+action]
    for game in range(4):
        values = returns[game*720:(game+1)*720]
        assert np.isnan(values[-1])
        assert values[0] in (-1,0,1) and np.all(values[:-1] == values[0])
    result = subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=30)
    assert result.returncode != 0 and "output exists" in result.stderr
