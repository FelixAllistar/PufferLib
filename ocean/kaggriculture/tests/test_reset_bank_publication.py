import csv
import ctypes
import json
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build_replay_state_bank as bank
import replay_native as native


@pytest.mark.parametrize("corrupt", [False, True])
def test_native_bank_publication(tmp_path, corrupt):
    # Native-generated frames qualify serialization, not official game parity.
    library = tmp_path / "core.so"
    subprocess.run(["cc", "-x", "c", "-O2", "-shared", "-fPIC",
                    str(ROOT / "core.h"), "-lm", "-o", str(library)], check=True)
    lib = native.load_core(library)
    episode = {"name": "kaggriculture", "module_version": "1.32.7",
               "id": 7, "configuration": {"seed": 123, "episodeSteps": 3},
               "statuses": ["DONE", "DONE"], "rewards": [0, 0], "steps": []}
    cfg = native.replay_config(episode)
    state = lib.kg_create(ctypes.byref(cfg))
    actions = (native.CAction * 2)(native.c_action({}), native.c_action({}))
    try:
        for turn in range(3):
            if turn:
                lib.kg_step(state, actions)
            snapshot = native.c_snapshot(lib, state)
            frame = []
            for player in range(2):
                obs = {key: snapshot[key] for key in
                       ("step", "day", "hour", "farms", "market", "town")}
                obs["private"] = snapshot["privates"][player]
                frame.append({"observation": obs, "action": {},
                              "status": "DONE" if snapshot["done"] else "ACTIVE"})
            episode["steps"].append(frame)
        episode["rewards"] = [lib.kg_player_money(state, p) for p in range(2)]
    finally:
        lib.kg_destroy(state)
    if corrupt:
        episode["steps"][-1][0]["observation"]["hour"] += 1
    source = tmp_path / "episode.json"
    source.write_text(json.dumps(episode))
    index = tmp_path / "index.tsv"
    with index.open("w") as stream:
        writer = csv.DictWriter(stream,
            fieldnames=["source", "episode_id", "turn", "player", "state_key"], delimiter="\t")
        writer.writeheader()
        writer.writerow(dict(source=str(source), episode_id=7, turn=0,
                             player=0, state_key="7:0:0"))
    output = tmp_path / "reset.kgb"
    args = bank.parse_args([str(source), "--index", str(index), "--output", str(output),
                            "--lib", str(library), "--skip-incompatible-episodes"])
    report = bank.build_bank(args)
    payload = output.read_bytes()
    magic, version, native_version, size, count, reserved = bank.BANK_HEADER.unpack_from(payload)
    assert (magic, version, native_version, size, reserved) == (
        bank.BANK_MAGIC, 1, lib.kg_state_serialization_version(),
        lib.kg_state_serialized_size(), 0)
    assert count == report["record_count"] == (0 if corrupt else 1)
    assert len(payload) == bank.BANK_HEADER.size + count * size
    if not corrupt:
        state = lib.kg_create(ctypes.byref(cfg))
        try:
            data = ctypes.create_string_buffer(payload[bank.BANK_HEADER.size:])
            assert lib.kg_state_deserialize(state, data, size)
            assert lib.kg_state_step(state) == 0 and not lib.kg_done(state)
        finally:
            lib.kg_destroy(state)
    else:
        assert report["skipped"]["parity_incompatible_episode"] == 1
    with pytest.raises(FileExistsError):
        bank.build_bank(args)
