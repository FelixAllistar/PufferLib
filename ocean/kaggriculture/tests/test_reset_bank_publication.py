import csv
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import zipfile
import hashlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build_replay_state_bank as bank
import replay_native as native
import build_diverse_reset_bank as diverse
import audit_diverse_reset_bank as audit


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
    archive = tmp_path / 'kaggriculture-episodes-2026-09-11.zip'
    with zipfile.ZipFile(archive, 'w') as stream:
        stream.writestr('episode.json', json.dumps(episode))
    options = dict(output=str(tmp_path / 'diverse'), episodes_per_day=1,
                   holdout_date='2026-09-12', lib_sha256='fixture',
                   reserve_gib=0, lib=str(library))
    summary = diverse.process_archive((str(archive), options))
    assert diverse.process_archive((str(archive), options)) == summary
    report = diverse.merge(options, [summary])
    split = diverse.split_for('7', 123, '2026-09-11', '2026-09-12')
    name = 'full' if split == 'train' else split
    path = Path(report['banks'][name]['path'])
    expected = {name: getattr(cfg, name) for name, _ in native.CConfig._fields_
                if name != 'seed'}
    if corrupt:
        assert report['banks'][name]['states'] == 0
        with pytest.raises(ValueError, match='empty bank'):
            audit.audit_bank(path, lib, expected)
    else:
        result, seeds, episodes = audit.audit_bank(path, lib, expected)
        assert result['states'] == 2 and seeds == {'123'} and episodes == {'7'}
        damaged = bytearray(path.read_bytes())
        damaged[-1] ^= 1
        path.write_bytes(damaged)
        with pytest.raises(ValueError, match='checksum mismatch'):
            audit.audit_bank(path, lib, expected)


def test_multiprocess_cli_and_audit(tmp_path):
    library = tmp_path / 'core.so'
    subprocess.run(['cc', '-x', 'c', '-O2', '-shared', '-fPIC',
                    str(ROOT / 'core.h'), '-lm', '-o', str(library)], check=True)
    lib = native.load_core(library)
    seeds = [next(seed for seed in range(100) if diverse.split_for(
        'unused', seed, '2026-09-10', '2026-09-12') == split)
        for split in ('train', 'holdout')]
    seeds.append(101)
    archives = []
    for index, seed in enumerate(seeds):
        episode = dict(name='kaggriculture', module_version='1.32.7', id=index,
                       configuration=dict(seed=seed), statuses=['DONE', 'DONE'],
                       rewards=[0, 0], steps=[])
        cfg = native.replay_config(episode)
        state = lib.kg_create(ctypes.byref(cfg))
        actions = (native.CAction * 2)(native.c_action({}), native.c_action({}))
        try:
            for turn in range(cfg.episode_steps):
                if turn:
                    lib.kg_step(state, actions)
                snapshot = native.c_snapshot(lib, state)
                frame = []
                for player in range(2):
                    observation = {key: snapshot[key] for key in
                                   ('step', 'day', 'hour', 'farms', 'market', 'town')}
                    observation['private'] = snapshot['privates'][player]
                    frame.append(dict(observation=observation, action={},
                        status='DONE' if snapshot['done'] else 'ACTIVE'))
                episode['steps'].append(frame)
            assert lib.kg_done(state)
            episode['rewards'] = [lib.kg_player_money(state, p) for p in range(2)]
        finally:
            lib.kg_destroy(state)
        archive = tmp_path / f'kaggriculture-episodes-2026-09-{10 + index}.zip'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as stream:
            stream.writestr('episode.json', json.dumps(episode))
        archives.append(str(archive))
    snapshots = []
    for jobs in (1, 2):
        output = tmp_path / f'workers{jobs}'
        command = [sys.executable, str(ROOT / 'build_diverse_reset_bank.py'), *archives,
                   '--output', str(output), '--lib', str(library), '--jobs', str(jobs),
                   '--episodes-per-day', '1', '--holdout-date', '2026-09-12',
                   '--reserve-gib', '0', '--min-full-states', '8']
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
        subprocess.run([sys.executable, str(ROOT / 'audit_diverse_reset_bank.py'),
                        '--directory', str(output), '--lib', str(library),
                        '--config', str(ROOT.parents[1] / 'config/kaggriculture.ini'),
                        '--min-full-states', '8'],
                       check=True, capture_output=True, text=True, timeout=120)
        report = json.loads((output / 'audit.json').read_text())
        assert report['passed']
        for split in ('full', 'holdout', 'future'):
            assert 8 <= report['banks'][split]['states'] <= 12
        files = sorted(output.glob('*.kgb')) + sorted(output.glob('*.manifest.tsv'))
        before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files}
        # Same-input resume must preserve every published bank and manifest byte.
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
        assert before == {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files}
        snapshots.append(before)
    assert snapshots[0] == snapshots[1]
