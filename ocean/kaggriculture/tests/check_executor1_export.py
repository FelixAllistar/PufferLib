"""Exact export parity against the native library used by a remote trainer.

Compare all 1280 observation bytes, 1058 mask bits and committed action fields.
Run with OPENBLAS_NUM_THREADS=1. Never updates a checkpoint or training config.
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import sys
import time

import numpy as np


def signature(action):
    units = [action.farmer, *action.hands[:action.hand_count]]
    return ([(u.op, u.arg if u.op in (5, 7, 14) else -1,
              u.n if u.op in (5, 14) else 0) for u in units],
            [(m.op, m.item, m.n if m.op < 4 else 0)
             for m in action.market[:action.market_count]])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--submission', type=Path)
    parser.add_argument('--lib', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--seeds', default='7,42')
    parser.add_argument('--sweep-every', type=int, default=120)
    parser.add_argument('--steps', type=int, default=720)
    args = parser.parse_args()
    sys.path.insert(0, str(args.root/'ocean/kaggriculture'))
    sys.path.insert(0, str(args.submission or args.root/'ocean/kaggriculture/submission'))
    os.environ['PUFFERLIB_MODEL_PATH'] = '/nonexistent/parity-model'
    from replay_native import load_core, CConfig, CAction, c_action, c_snapshot
    import main as policy
    from native_macro_runtime import NativeMacroRuntime
    lib = load_core(args.lib.resolve())
    lib.kg_experiment_context_create.argtypes = [c.c_int]*4
    lib.kg_experiment_context_create.restype = c.c_void_p
    lib.kg_experiment_context_free.argtypes = [c.c_void_p]
    lib.kg_experiment_context_view.argtypes = [c.c_void_p, c.c_void_p, c.c_int, c.c_void_p, c.c_void_p]
    lib.kg_experiment_context_action.argtypes = [c.c_void_p, c.c_void_p, c.c_int, c.c_void_p, c.POINTER(CAction)]
    model_path = args.model.resolve()
    views = actions_checked = 0
    seen = set()
    peak_hands = 0
    started = time.monotonic()
    policy._DETERMINISTIC = False
    if hasattr(lib, 'kg_export_fixture'):
        lib.kg_export_fixture.argtypes = [c.c_void_p, c.c_int]
        fixture_ops = set()
        for fixture in range(3):
            cfg = CConfig()
            lib.kg_config_default(c.byref(cfg))
            state = lib.kg_create(c.byref(cfg))
            context = lib.kg_experiment_context_create(1, 1, 1, 1)
            runtime = NativeMacroRuntime(mode=2, executor_version=1)
            try:
                lib.kg_export_fixture(state, fixture)
                snapshot = c_snapshot(lib, state)
                obs = dict(snapshot, player=0, private=snapshot['privates'][0])
                obs.pop('privates', None)
                native_obs = np.zeros(1280, dtype=np.uint8)
                native_mask = np.zeros(1058, dtype=np.uint8)
                lib.kg_experiment_context_view(context, state, 0, native_obs.ctypes.data, native_mask.ctypes.data)
                np.testing.assert_array_equal(runtime.fill_observation(obs, policy.encode_observation(obs, 1)), native_obs)
                np.testing.assert_array_equal(runtime.action_mask(obs), native_mask.astype(bool))
                for macro in range(44):
                    for q in range(8):
                        for target in range(5):
                            requests = np.zeros(47, dtype=np.float32)
                            requests[:3] = macro, q, target
                            native = CAction()
                            lib.kg_experiment_context_action(context, state, 0, requests.ctypes.data, c.byref(native))
                            portable = c_action(runtime.decode(obs, requests))
                            assert signature(native) == signature(portable), ('fixture', fixture, macro, q, target, signature(native), signature(portable))
                            fixture_ops.update(u.op for u in [native.farmer, *native.hands[:native.hand_count]])
                print(f'PASS fixture={fixture} hands={len(obs["farms"][0]["hands"])} comparisons=1760', flush=True)
            finally:
                lib.kg_experiment_context_free(context)
                lib.kg_destroy(state)
        assert 10 in fixture_ops, 'fertilization not exercised'
        print(f'PASS fixtures operations={sorted(fixture_ops)}', flush=True)
    for seed in map(int, args.seeds.split(',')):
        for learner_seat in (0, 1):
            cfg = CConfig()
            lib.kg_config_default(c.byref(cfg))
            cfg.seed = seed
            state = lib.kg_create(c.byref(cfg))
            context = lib.kg_experiment_context_create(1, 1, 1, 1)
            runtime = NativeMacroRuntime(mode=2, executor_version=1)
            model = policy.NativeMinGRU(model_path)
            policy._RNG = np.random.default_rng(seed * 2 + learner_seat)
            try:
                for turn in range(args.steps):
                    snapshot = c_snapshot(lib, state)
                    committed = (CAction * 2)()
                    for seat in (0, 1):
                        obs = dict(snapshot, player=seat, private=snapshot['privates'][seat])
                        obs.pop('privates', None)
                        peak_hands = max(peak_hands, len(obs['farms'][seat]['hands']))
                        native_obs = np.zeros(1280, dtype=np.uint8)
                        native_mask = np.zeros(1058, dtype=np.uint8)
                        lib.kg_experiment_context_view(context, state, seat, native_obs.ctypes.data, native_mask.ctypes.data)
                        portable_obs = runtime.fill_observation(obs, policy.encode_observation(obs, 1))
                        mask = runtime.action_mask(obs)
                        label = f'seed={seed} learner={learner_seat} turn={turn} seat={seat}'
                        np.testing.assert_array_equal(portable_obs, native_obs, err_msg='OBS '+label)
                        np.testing.assert_array_equal(mask, native_mask.astype(bool), err_msg='MASK '+label)
                        views += 1

                        def compare(requests):
                            nonlocal actions_checked
                            native = CAction()
                            lib.kg_experiment_context_action(context, state, seat, requests.ctypes.data, c.byref(native))
                            portable = c_action(runtime.decode(obs, requests))
                            assert signature(native) == signature(portable), (
                                label, requests[:3].tolist(), signature(native), signature(portable))
                            actions_checked += 1
                            seen.update(u.op for u in [native.farmer, *native.hands[:native.hand_count]])
                            return native

                        if args.sweep_every and turn % args.sweep_every == 0:
                            # Include masked IDs, large batches and every quadrant.
                            for macro in range(44):
                                for q, target in ((0, 0), (7, 0), (1, 1), (2, 2), (3, 3), (4, 4)):
                                    requests = np.zeros(47, dtype=np.float32)
                                    requests[:3] = macro, q, target
                                    compare(requests)
                        requests = np.zeros(47, dtype=np.float32)
                        if seat == learner_seat:
                            requests[:] = policy._sample_heads(model.forward(portable_obs), mask)
                            committed[seat] = compare(requests)
                        else:
                            # Keep both macro contexts aligned, then evolve the other farm with rules.
                            compare(requests)
                            lib.kg_rule_action(state, seat, c.byref(committed[seat]))
                    lib.kg_step(state, committed)
                    if turn % 120 == 0:
                        print(f'parity {label} views={views} actions={actions_checked} elapsed={time.monotonic()-started:.1f}s', flush=True)
                print(f'PASS episode seed={seed} learner={learner_seat} cash={lib.kg_player_money(state,learner_seat)}', flush=True)
            finally:
                lib.kg_experiment_context_free(context)
                lib.kg_destroy(state)
    print(f'PASS executor1 views={views} actions={actions_checked} peak_hands={peak_hands} operations={sorted(seen)} elapsed={time.monotonic()-started:.1f}s', flush=True)


if __name__ == '__main__':
    main()
