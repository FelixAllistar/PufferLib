#!/usr/bin/env python3
"""Make versioned float-encoder BC + expert-return data from checked tapes.

Run from the repo root, with uv and numpy. No Kaggle import or GPU required.
Training sequences precede the immutable episode-level holdout. Terminal rows
have NaN returns and no action labels. Returns use the production stateful
reward implementation, not raw final cash. See entity_bc_labels.py caveats.
"""
from __future__ import annotations

import argparse
import collections
import ctypes as C
import gzip
import hashlib
import json
import pathlib
import struct
import time

import numpy as np

import entity_bc_labels as labels
import multi_intent_labels
import multi_bc_labels
import replay_native as native

HEADER = struct.Struct("<16IQQd")


def discounted_returns(rewards, gamma):
    output = np.full(len(rewards) + 1, np.nan, np.float32)
    value = 0.0
    for t in range(len(rewards) - 1, -1, -1):
        value = float(rewards[t]) + gamma * value
        output[t] = value
    return output


def load_bridge(path):
    lib = native.load_core(path)
    ptr = C.c_void_p
    lib.kag_bc_create.argtypes = [C.POINTER(native.CConfig), C.c_char_p]
    lib.kag_bc_create.restype = ptr
    lib.kag_bc_destroy.argtypes = [ptr]
    lib.kag_bc_source_hash.restype = C.c_uint64
    lib.kag_bc_semantics_hash.argtypes = [ptr]
    lib.kag_bc_semantics_hash.restype = C.c_uint64
    lib.kag_bc_gamma.argtypes = [ptr]
    lib.kag_bc_gamma.restype = C.c_double
    lib.kag_bc_abi.argtypes = [C.c_int]
    lib.kag_bc_executor.argtypes = [ptr]
    abi = tuple(lib.kag_bc_abi(i) for i in range(4))
    if abi != (labels.OBS, labels.HEADS, labels.MASK, labels.POLICY_VERSION):
        raise ValueError(f"bridge/dataset policy ABI mismatch: {abi}")
    lib.kag_bc_state.argtypes = [ptr]
    lib.kag_bc_state.restype = ptr
    lib.kag_bc_positions.argtypes = [ptr, C.c_int, ptr, C.c_int]
    lib.kag_bc_work_facts.argtypes = [ptr,C.c_int,C.POINTER(native.CAction),ptr,C.c_int]
    if hasattr(lib, 'kag_bc_effects'):
        lib.kag_bc_effects.argtypes = [ptr,C.c_int,C.POINTER(native.CAction),ptr,ptr,C.c_int]
    lib.kag_bc_view.argtypes = [ptr, C.c_int, ptr, ptr]
    lib.kag_bc_teacher_mask.argtypes = [ptr, C.c_int, ptr, ptr]
    lib.kag_bc_decode.argtypes = [ptr, C.c_int, ptr, C.POINTER(native.CAction)]
    lib.kag_bc_step.argtypes = [ptr, C.POINTER(native.CAction), C.c_int, ptr, ptr]
    return lib


def build_game(lib, tape, seat, profile, annotate=None):
    cfg = native.replay_config(tape)
    n = tape["frames"]
    if tape.get("parity_frames") != n or n != len(tape["actions"]) + 1:
        raise ValueError("requires a complete, frame-by-frame parity-checked tape")
    context = lib.kag_bc_create(C.byref(cfg), str(profile).encode())
    if not context:
        raise RuntimeError("native replay allocation failed")
    observations = np.empty((n, labels.OBS), np.float32)
    actions = np.full((n, labels.HEADS), -1, np.float32)
    masks = np.empty((n, (labels.MASK + 7) // 8), np.uint8)
    reward = np.empty(n - 1, np.float32)
    mask = np.empty(labels.MASK, np.uint8)
    pair_reward = np.empty(2, np.float32)
    positions = np.empty((native.KG_MAX_HANDS + 1, 2), np.int32)
    facts = np.empty((native.KG_MAX_HANDS + 1, 4), np.int32)
    counts = collections.Counter()
    def decode(heads):
        action = native.CAction()
        if not lib.kag_bc_decode(context, seat, heads.ctypes.data, C.byref(action)):
            raise RuntimeError("decoder failed")
        return action
    def teacher_mask(heads, output):
        if not lib.kag_bc_teacher_mask(context, seat, heads.ctypes.data, output.ctypes.data):
            raise RuntimeError("teacher prefix mask failed")
    def work_facts(action):
        out = np.empty_like(facts)
        count = lib.kag_bc_work_facts(context,seat,C.byref(action),out.ctypes.data,out.size)
        if count < 1: raise RuntimeError("work preview failed")
        return out[:count]
    try:
        gamma = lib.kag_bc_gamma(context)
        semantics = lib.kag_bc_semantics_hash(context)
        executor = lib.kag_bc_executor(context)
        for t in range(n):
            if not lib.kag_bc_view(context, seat, observations[t].ctypes.data, mask.ctypes.data):
                raise RuntimeError("view failed")
            masks[t] = np.packbits(mask, bitorder="little")
            if t == n - 1:
                break
            pair = (native.CAction * 2)(*(native.c_action(a) for a in tape["actions"][t]))
            units = lib.kag_bc_positions(context, seat, positions.ctypes.data, positions.size)
            if units < 1:
                raise RuntimeError("cannot annotate worker positions")
            if executor == 2:
                if lib.kag_bc_work_facts(context,seat,C.byref(pair[seat]),facts.ctypes.data,facts.size) != units:
                    raise RuntimeError("cannot derive current-state work facts")
                actions[t], history, report = multi_bc_labels.project(pair[seat], mask, decode,
                    positions[:units], teacher_mask, cfg.board_size, facts[:units], work_facts)
                counts.update(report)
                reason = "multi_observed_strategy"
            else:
                actions[t], history, reason = labels.project(pair[seat], mask, decode)
            # BC must use the same prefix-dependent masks as PPO, including
            # parameters disabled for the selected macro. Skipped rows retain
            # a view but have no actor gradient.
            if np.any(actions[t] >= 0):
                teacher_mask(history, mask)
                masks[t] = np.packbits(mask, bitorder="little")
            if annotate is not None:
                annotation = multi_intent_labels.annotate(tape["actions"][t][seat], positions[:units],
                         episode_id=tape["info"]["EpisodeId"], player=seat, step=t,
                         single_label=actions[t] if executor == 1 else np.full(labels.HEADS, -1),
                         reason=reason)
                if executor == 2:
                    annotation['multi_2_2'] = dict(heads=actions[t].astype(int).tolist(), coverage=dict(report))
                annotate(annotation)
            counts[reason] += 1
            if actions[t, 0] >= 0:
                counts[f"macro_{int(actions[t, 0])}"] += 1
            if not lib.kag_bc_step(context, pair, seat, history.ctypes.data, pair_reward.ctypes.data):
                raise RuntimeError(f"step failed at {t}")
            reward[t] = pair_reward[seat]
        state = lib.kag_bc_state(context)
        cash = [lib.kg_player_money(state, p) for p in range(2)]
        if not lib.kg_done(state) or cash != tape["rewards"]:
            raise ValueError(f"replayed terminal differs: {cash} != {tape['rewards']}")
        if not np.isfinite(observations).all() or not np.isfinite(reward).all():
            raise ValueError("nonfinite observations or rewards")
        return observations, actions, masks, discounted_returns(reward, gamma), semantics, gamma, counts
    finally:
        lib.kag_bc_destroy(context)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--profile", type=pathlib.Path, required=True)
    parser.add_argument("--lib", type=pathlib.Path, required=True)
    parser.add_argument("--teacher", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    multi_path = args.output.with_suffix(".intents.jsonl.gz")
    if args.output.exists() or args.output.with_suffix(".json").exists() or multi_path.exists():
        parser.error("output already exists; choose a new versioned path")
    manifest = json.loads(args.manifest.read_text())
    entries = sorted(manifest["cache"], key=lambda x: (x["split"] == "holdout", x["episode_id"]))
    if {e["split"] for e in entries} != {"train", "holdout"}:
        parser.error("both train and holdout episodes are required")
    if len({e["episode_id"] for e in entries}) != len(entries):
        parser.error("duplicate episodes are not allowed")
    lib = load_bridge(args.lib.resolve())
    source_hash = lib.kag_bc_source_hash()
    if not source_hash:
        parser.error("bridge lacks its source fingerprint; rebuild with the Makefile")
    games = []
    records = []
    start = time.monotonic()
    reference_contract = None
    annotations = []
    for entry in entries:
        with gzip.open(entry["path"], "rt") as stream:
            tape = json.load(stream)
        names = tape["info"]["TeamNames"]
        if names.count(args.teacher) != 1:
            raise ValueError(f"teacher must uniquely identify one seat: {names}")
        seat = names.index(args.teacher)
        game = build_game(lib, tape, seat, args.profile.resolve(), annotations.append)
        contract = (len(game[0]), game[4], game[5])
        if reference_contract is not None and contract != reference_contract:
            raise ValueError("mixed episode or reward/controller contracts")
        reference_contract = contract
        games.append(game)
        records.append(dict(episode_id=entry["episode_id"], split=entry["split"], player=seat,
                            tape=entry["path"], source_sha256=tape["source_sha256"], labels=dict(game[6])))
        print(f"{len(games)}/{len(entries)} episode={entry['episode_id']} labels={dict(game[6])}", flush=True)
    steps, semantics, gamma = reference_contract
    validation = sum(e["split"] == "holdout" for e in entries)
    profile = args.profile.read_text()
    import configparser
    settings = configparser.ConfigParser(); settings.read_string(profile)
    features = settings.getint("env", "macro_score_features", fallback=0)
    executor = settings.getint("env", "macro_executor_version")
    header = HEADER.pack(0x4b414742, 3, steps * len(games), labels.OBS, labels.HEADS,
                         (labels.MASK + 7) // 8, len(games), steps, 4, 3, labels.POLICY_VERSION, 2, executor, 1,
                         features, validation, source_hash, semantics, gamma)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation: never overwrite a dataset used by a running trainer.
    with args.output.open("xb") as stream:
        stream.write(header)
        for part in range(4):
            for game in games:
                stream.write(game[part].tobytes(order="C"))
    with gzip.open(multi_path, "xt", encoding="utf-8") as stream:
        for annotation in annotations:
            stream.write(json.dumps(annotation, separators=(",", ":")) + "\n")
    metadata = dict(format="kaggriculture_entity_bc_v3", teacher=args.teacher,
                    policy_version=labels.POLICY_VERSION, executor=executor,
                    source_hash=f"{source_hash:016x}", semantics_hash=f"{semantics:016x}",
                    gamma=gamma, train_games=len(games)-validation, validation_games=validation,
                    profile=profile, records=records, wall_seconds=time.monotonic()-start,
                    history="canonical native-compatible projected heads; unidentified production uses AUTO/HOLD",
                    warning="Primitive expert is not a macro policy. Strategic projection is lossy; rollout evaluation is required.",
                    reset_trajectories=False, submission_revision_verified=False,
                    multi_intent_sidecar=str(multi_path), raw_action_records=len(annotations),
                    multi_request_rows=sum(a["strategic_request_count"] > 1 for a in annotations),
                    multi_intent_consumed_by_single_2_1=False,
                    sha256=hashlib.sha256(args.output.read_bytes()).hexdigest())
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"wrote {args.output} ({len(games)-validation} train/{validation} holdout)", flush=True)


if __name__ == "__main__":
    main()
