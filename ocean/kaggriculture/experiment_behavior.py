#!/usr/bin/env python3
"""CPU-native diagnostic episodes; never substitute these for GPU match scores."""
import argparse
import ctypes as ct
import importlib.util
import json
import os
from pathlib import Path
import sys
import time

import numpy as np
from eval_observation_versions import observation_version, executor_version
from replay_native import CAction, CConfig, load_core

HERE = Path(__file__).resolve().parent
FIELDS = ('cash', 'plots', 'plants', 'animals', 'cows', 'milk_harvested',
          'milk_ready', 'productive_extra_tiles', 'sales_revenue',
          'purchase_spend', 'neglect_deaths', 'hands')


def summary(episodes):
    if not episodes:
        raise ValueError('No completed behavior episodes')
    result = {'games': len(episodes)}
    for event in ('first_cow_turn', 'first_milk_turn'):
        observed = [e[event] for e in episodes if e[event] is not None]
        result[event + '_never_fraction'] = 1 - len(observed) / len(episodes)
        result[event + '_conditional_mean'] = float(np.mean(observed)) if observed else None
    result['cow_by_150_fraction'] = sum(e['first_cow_turn'] is not None and e['first_cow_turn'] <= 150 for e in episodes) / len(episodes)
    result['milk_by_300'] = float(np.mean([e['milk_by_300'] for e in episodes]))
    result['productive_extra_tile_turns'] = float(np.mean([e['productive_extra_tile_turns'] for e in episodes]))
    for field in FIELDS:
        result['final_' + field] = float(np.mean([e['final'][field] for e in episodes]))
    return result


def evaluate(model, opponents, seeds, deterministic, libpath, env_values=None, spending=False):
    os.environ['PUFFERLIB_MODEL_PATH'] = '/nonexistent/experiment-probe.bin'
    spec = importlib.util.spec_from_file_location('experiment_policy', HERE / 'submission/main.py')
    policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(policy)
    lib = load_core(libpath)
    lib.kg_experiment_observation.argtypes = [ct.c_void_p, ct.c_int, ct.c_int, ct.c_void_p]
    lib.kg_experiment_metrics.argtypes = [ct.c_void_p, ct.c_int, ct.c_void_p]
    lib.kg_experiment_mask.argtypes = [ct.c_void_p, ct.c_int, ct.c_void_p]
    lib.kg_experiment_action.argtypes = [ct.c_void_p, ct.c_int, ct.c_void_p, ct.POINTER(CAction)]
    lib.kg_experiment_action.restype = None
    lib.kg_experiment_observation.restype = None
    lib.kg_experiment_metrics.restype = None
    lib.kg_experiment_mask.restype = None
    lib.kg_experiment_context_create.argtypes = [ct.c_int] * 4
    lib.kg_experiment_context_create.restype = ct.c_void_p
    lib.kg_experiment_context_free.argtypes = [ct.c_void_p]
    lib.kg_experiment_context_free.restype = None
    lib.kg_experiment_context_view.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_int, ct.c_void_p, ct.c_void_p]
    lib.kg_experiment_context_view.restype = None
    lib.kg_experiment_context_action.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_int, ct.c_void_p, ct.POINTER(CAction)]
    lib.kg_experiment_context_action.restype = None
    if spending:
        lib.kg_spending_metrics.argtypes = [ct.c_void_p, ct.c_int, ct.c_void_p]
        lib.kg_spending_metrics.restype = None
    episodes = []
    learner = policy.NativeMinGRU(str(model))
    own_version = observation_version(model)
    own_executor = executor_version(model)
    for opponent in opponents:
        enemy = policy.NativeMinGRU(str(opponent))
        enemy_version = observation_version(opponent)
        enemy_executor = executor_version(opponent)
        for seed in seeds:
            for seat in (0, 1):
                cfg = CConfig()
                lib.kg_config_default(ct.byref(cfg))
                for field, value in (env_values or {}).items():
                    if hasattr(cfg, field) and field != 'seed':
                        setattr(cfg, field, float(value) if field == 'weed_spawn_chance' else int(float(value)))
                cfg.seed = seed
                state = lib.kg_create(ct.byref(cfg))
                learner.reset(); enemy.reset()
                policies = [learner, enemy] if seat == 0 else [enemy, learner]
                versions = [own_version, enemy_version] if seat == 0 else [enemy_version, own_version]
                executors = [own_executor, enemy_executor] if seat == 0 else [enemy_executor, own_executor]
                context = lib.kg_experiment_context_create(*versions, *executors)
                if not context:
                    lib.kg_destroy(state)
                    raise RuntimeError('Cannot create versioned executor diagnostic context')
                generators = [np.random.default_rng(seed * 2 + p) for p in range(2)]
                episode = dict(seed=seed, seat=seat, opponent=str(opponent),
                               first_cow_turn=None, first_milk_turn=None,
                               milk_by_300=0, productive_extra_tile_turns=0,
                               plot_unlock_turns={}, trajectory=[])
                obs = np.zeros(1280, np.uint8)
                mask = np.zeros(1058, np.uint8)
                metrics = np.zeros(12, np.float64)
                try:
                    while True:
                        turn = lib.kg_state_step(state)
                        lib.kg_experiment_metrics(state, seat, metrics.ctypes.data)
                        values = dict(zip(FIELDS, metrics.tolist()))
                        if spending:
                            extra = np.zeros(17, np.float64)
                            lib.kg_spending_metrics(state, seat, extra.ctypes.data)
                            values.update(zip(('geese', 'cow_count', 'sheep', 'wheat', 'carrot',
                                               'tomato', 'strawberry', 'melon',
                                               'unplaced_geese', 'unplaced_cows', 'unplaced_sheep',
                                               'seed_wheat', 'seed_carrot', 'seed_tomato',
                                               'seed_strawberry', 'seed_melon', 'empty_animal_housing'), extra.tolist()))
                            values['occupied_animals'] = sum(extra[:3])
                            values['animal_housing'] = values['occupied_animals'] + values['empty_animal_housing']
                            values['land_spend'] = (0, 1000, 3000, 7000)[int(values['plots'])-1]
                            values['hire_spend'] = (cfg.starting_money + values['sales_revenue']
                                                   - values['purchase_spend'] - values['cash']
                                                   - values['land_spend'])
                            if values['cow_count'] != values['cows'] or values['empty_animal_housing'] < 0:
                                raise ValueError('Inconsistent livestock diagnostic')
                            if values['hire_spend'] < -0.1:
                                raise ValueError('Cash accounting failed')
                        if values['cows'] > 0 and episode['first_cow_turn'] is None:
                            episode['first_cow_turn'] = turn
                        if values['milk_ready'] + values['milk_harvested'] > 0 and episode['first_milk_turn'] is None:
                            episode['first_milk_turn'] = turn
                        for plot in range(1, int(values['plots']) + 1):
                            episode['plot_unlock_turns'].setdefault(str(plot), turn)
                        if turn <= 300:
                            episode['milk_by_300'] = values['milk_harvested']
                        if turn % 24 == 0 or lib.kg_done(state):
                            episode['trajectory'].append(dict(turn=turn, **values))
                        if lib.kg_done(state):
                            episode['final'] = values
                            break
                        episode['productive_extra_tile_turns'] += values['productive_extra_tiles']
                        actions = (CAction * 2)()
                        for player in (0, 1):
                            lib.kg_experiment_context_view(context, state, player, obs.ctypes.data, mask.ctypes.data)
                            logits = policies[player].forward(obs)
                            requests = np.zeros(47, np.float32)
                            # Macro mode 2 consumes just intent/quantity/quadrant.
                            for head in range(3):
                                lo, hi = policy.HEAD_OFFSETS[head:head+2]
                                legal = np.flatnonzero(mask[lo:hi])
                                scores = logits[lo:hi][legal]
                                if deterministic:
                                    action = legal[np.argmax(scores)]
                                else:
                                    probabilities = np.exp(scores - scores.max())
                                    action = generators[player].choice(legal, p=probabilities/probabilities.sum())
                                requests[head] = action
                            lib.kg_experiment_context_action(context, state, player, requests.ctypes.data, ct.byref(actions[player]))
                        lib.kg_step(state, actions)
                    episodes.append(episode)
                finally:
                    lib.kg_experiment_context_free(context)
                    lib.kg_destroy(state)
    return {'backend': 'cpu_native_numpy_diagnostic', 'deterministic': deterministic,
            'summary': summary(episodes), 'episodes': episodes}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--model', required=True)
    parser.add_argument('--opponents', nargs='+', required=True)
    parser.add_argument('--seeds', nargs='+', type=int, default=[91001])
    parser.add_argument('--stochastic', action='store_true')
    parser.add_argument('--lib', default=str(HERE / 'build/libkag_experiment.so'))
    parser.add_argument('--output', required=True)
    parser.add_argument('--env-json')
    parser.add_argument('--spending', action='store_true')
    args = parser.parse_args()
    start = time.monotonic()
    values = json.loads(Path(args.env_json).read_text()) if args.env_json else {}
    report = evaluate(args.model, args.opponents, args.seeds, not args.stochastic, args.lib, values, args.spending)
    report['seconds'] = time.monotonic() - start
    Path(args.output).write_text(json.dumps(report, indent=2))
    print(json.dumps(report['summary']), flush=True)


if __name__ == '__main__':
    main()
