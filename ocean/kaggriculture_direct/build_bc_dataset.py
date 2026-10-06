#!/usr/bin/env python3
"""Stream parity-checked primitive replay labels into a direct ABI-6 BC dataset.

Supports multiple teachers/both seats, with episode-level train/holdout isolation.
No strategic projection, teacher action search, GPU, or PPO is involved.
"""
from __future__ import annotations

import argparse
import configparser
from concurrent.futures import ProcessPoolExecutor
import ctypes as C
import gzip
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "kaggriculture"))
import replay_native as native

HEADER = struct.Struct("<16IQQd")
OBS, HEADS, LOGITS, STEPS = 6112, 30, 29030, 720
PACKED = (LOGITS + 7) // 8


def load_bridge(path):
    lib = native.load_core(path)
    ptr = C.c_void_p
    lib.kag_bc_abi.argtypes = [C.c_int]
    if tuple(lib.kag_bc_abi(i) for i in range(4)) != (OBS, HEADS, LOGITS, 6):
        raise ValueError("requires direct ABI-6 bridge with paired critic features")
    lib.kag_bc_create.argtypes = [C.POINTER(native.CConfig), C.c_char_p]
    lib.kag_bc_create.restype = ptr
    lib.kag_bc_destroy.argtypes = [ptr]
    lib.kag_bc_state.argtypes = [ptr]
    lib.kag_bc_state.restype = ptr
    lib.kag_bc_source_hash.restype = C.c_uint64
    lib.kag_bc_semantics_hash.argtypes = [ptr]
    lib.kag_bc_semantics_hash.restype = C.c_uint64
    lib.kag_bc_view.argtypes = [ptr, C.c_int, ptr, ptr]
    lib.kag_direct_project.argtypes = [ptr, C.c_int, C.POINTER(native.CAction), ptr, ptr, ptr, ptr]
    lib.kag_bc_step.argtypes = [ptr, C.POINTER(native.CAction), C.c_int, ptr, ptr]
    return lib


def build_game(lib, tape, seat, profile):
    if tape.get("parity_frames") != STEPS or len(tape["actions"]) != STEPS - 1:
        raise ValueError("requires a complete 720-frame, parity-checked tape")
    config = native.replay_config(tape)
    context = lib.kag_bc_create(C.byref(config), str(profile).encode())
    if not context:
        raise RuntimeError("native replay allocation failed")
    obs = np.zeros((STEPS, OBS), np.float32)
    labels = np.full((STEPS, HEADS), -1, np.float32)
    masks = np.zeros((STEPS, PACKED), np.uint8)
    returns = np.full(STEPS, np.nan, np.float32)
    mask = np.empty(LOGITS, np.uint8)
    history = np.empty(HEADS, np.float32)
    rewards = np.empty(2, np.float32)
    counts = np.zeros(3, np.int32)
    try:
        for t in range(STEPS):
            if not lib.kag_bc_view(context, seat, obs[t].ctypes.data, mask.ctypes.data):
                raise RuntimeError("observation failed")
            if t == STEPS - 1:
                break
            pair = (native.CAction * 2)(*(native.c_action(a) for a in tape["actions"][t]))
            if not lib.kag_direct_project(context, seat, C.byref(pair[seat]), labels[t].ctypes.data,
                                         history.ctypes.data, mask.ctypes.data, counts.ctypes.data):
                raise ValueError(f"unrepresentable unit/order capacity at step {t}")
            masks[t] = np.packbits(mask, bitorder="little")
            if not lib.kag_bc_step(context, pair, seat, history.ctypes.data, rewards.ctypes.data):
                raise RuntimeError(f"step failed at {t}")
        state = lib.kag_bc_state(context)
        money = [lib.kg_player_money(state, p) for p in range(2)]
        if not lib.kg_done(state) or money != tape["rewards"]:
            raise ValueError(f"terminal replay mismatch: {money} != {tape['rewards']}")
        returns[:-1] = np.sign(money[seat] - money[1-seat])
        if not np.isfinite(obs).all():
            raise ValueError("nonfinite observation")
        return (obs, labels, masks, returns), lib.kag_bc_semantics_hash(context), counts.tolist()
    finally:
        lib.kag_bc_destroy(context)


def select_entries(manifest, teachers, tape_root=None):
    entries = manifest.get("cache")
    if entries is None:
        entries = [dict(r, path=r["tape"]) for r in manifest.get("records", [])]
    if not entries or {e["split"] for e in entries} != {"train", "holdout"}:
        raise ValueError("manifest must include train and holdout episodes")
    ids = [str(e["episode_id"]) for e in entries]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate episodes / split leakage")
    records = []
    for entry in sorted(entries, key=lambda e: (e["split"] == "holdout", str(e["episode_id"]))):
        path = Path(entry["path"])
        if tape_root is not None:
            path = tape_root / path.name
        entry = dict(entry, path=str(path.resolve()))
        with gzip.open(path, "rt") as stream:
            tape = json.load(stream)
        if str(tape["info"]["EpisodeId"]) != str(entry["episode_id"]):
            raise ValueError("episode identity mismatch")
        if entry.get("source_sha256", tape["source_sha256"]) != tape["source_sha256"]:
            raise ValueError("source digest mismatch")
        for seat, name in enumerate(tape["info"]["TeamNames"]):
            if not teachers or name in teachers:
                records.append(dict(entry, seat=seat, teacher=name))
    if {e["split"] for e in records} != {"train", "holdout"}:
        raise ValueError("selected teachers must have both train and holdout episodes")
    return records


def validate_profile(path):
    profile = configparser.ConfigParser(inline_comment_prefixes=("#", ";"))
    if not profile.read(path):
        raise ValueError(f"profile not found: {path}")
    if profile.getint("env", "reward_win_loss_draw") != 1 or profile.getfloat("train", "gamma") != 1:
        raise ValueError("direct BC targets require terminal WLD and gamma=1")
    if profile.getfloat("env", "reward_money") != 1:
        raise ValueError("terminal WLD requires reward_money=1")
    for key in ("reward_growth_land", "reward_growth_crop", "reward_growth_animal",
                "reward_alive_daily", "reward_quality_scale", "reward_quality_idle_cost", "potential_beta"):
        value = profile.getfloat("env", key)
        if value != 0:
            raise ValueError(f"direct BC targets require unshaped WLD: {key}={value}")


def convert_record(library, profile, record):
    lib = load_bridge(library)
    with gzip.open(record["path"],"rt") as stream:
        tape = json.load(stream)
    return build_game(lib,tape,record["seat"],profile)


def converted_records(records, library, profile, workers):
    if workers == 1:
        for record in records:
            yield convert_record(library,profile,record)
        return
    # Bound outstanding 20-MB trajectories; do not submit the entire corpus.
    with ProcessPoolExecutor(max_workers=workers) as pool:
        pending = {}
        for i in range(min(workers,len(records))):
            pending[i] = pool.submit(convert_record,library,profile,records[i])
        for i in range(len(records)):
            result = pending.pop(i).result()
            j = i+workers
            if j < len(records):
                pending[j] = pool.submit(convert_record,library,profile,records[j])
            yield result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--tape-root", type=Path, help="relocate cached replay tapes by basename")
    parser.add_argument("--lib", type=Path, default=Path("ocean/kaggriculture_direct/build/replay.so"))
    parser.add_argument("--profile", type=Path, default=Path("config/kaggriculture.ini"))
    parser.add_argument("--teacher", action="append", default=[], help="repeat for several teachers; default both seats")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.workers < 1 or args.workers > 16:
        parser.error("workers must be in 1..16")
    output = args.output.resolve()
    metadata_path = output.with_suffix(".json")
    if output.exists() or metadata_path.exists():
        parser.error("output exists; choose a new versioned dataset")
    manifest = json.loads(args.manifest.read_text())
    validate_profile(args.profile)
    records = select_entries(manifest, args.teacher, args.tape_root)
    lib = load_bridge(args.lib.resolve())
    source_hash = lib.kag_bc_source_hash()
    if not source_hash:
        parser.error("bridge must be built with its source fingerprint")
    games, count = len(records), len(records)*STEPS
    validation = sum(r["split"] == "holdout" for r in records)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="direct-bc-", dir=output.parent) as directory:
        staging = Path(directory)/"dataset.bc"
        shapes = [(count,OBS),(count,HEADS),(count,PACKED),(count,)]
        dtypes = [np.float32,np.float32,np.uint8,np.float32]
        size = HEADER.size + sum(int(np.prod(s))*np.dtype(d).itemsize for s,d in zip(shapes,dtypes))
        with staging.open("xb") as stream:
            stream.truncate(size)
        arrays, offset = [], HEADER.size
        for shape, dtype in zip(shapes,dtypes):
            arrays.append(np.memmap(staging,mode="r+",dtype=dtype,shape=shape,offset=offset))
            offset += arrays[-1].nbytes
        semantics = None
        results = converted_records(records,args.lib.resolve(),args.profile.resolve(),args.workers)
        for i, (record, (parts, contract, counts)) in enumerate(zip(records,results)):
            if semantics is not None and contract != semantics:
                raise ValueError("mixed environment semantics")
            semantics = contract
            for destination, part in zip(arrays,parts):
                destination[i*STEPS:(i+1)*STEPS] = part
            record["labels_valid_forced_filtered"] = counts
            print(f"{i+1}/{games} episode={record['episode_id']} seat={record['seat']} labels={counts}",flush=True)
        for array in arrays:
            array.flush()
        header = HEADER.pack(0x4b414742,3,count,OBS,HEADS,PACKED,games,STEPS,4,4,6,0,0,1,0,
                             validation,source_hash,semantics,1.0)
        with staging.open("r+b") as stream:
            stream.write(header)
        with staging.open("rb") as stream:
            digest = hashlib.file_digest(stream,"sha256").hexdigest()
        metadata = dict(format="kaggriculture_direct_bc_v6",sha256=digest,source_hash=f"{source_hash:016x}",
                        gamma=1,train_games=games-validation,validation_games=validation,records=records,
                        policy_version=6,observation_version=4,executor=0,
                        label_semantics="Exact representable direct actions; forced/blocked/unsupported heads ignored")
        meta_staging = Path(directory)/"dataset.json"
        meta_staging.write_text(json.dumps(metadata,indent=2)+"\n")
        # Exclusive publication; never replace an existing artifact.
        os.link(staging,output)
        os.link(meta_staging,metadata_path)
    print(f"wrote {output}: {games-validation} train / {validation} held-out seat trajectories")


if __name__ == "__main__":
    main()
