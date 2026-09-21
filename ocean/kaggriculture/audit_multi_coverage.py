"""Replay-only diagnostics: requested strategy, executor output, and conflicts.

Never runs a learned policy or changes a replay trajectory. Native previews use
only the current state. Original two-player actions advance the replay. Output
is exclusive and retains a bounded set of actionable counterexamples.
"""
from __future__ import annotations

import argparse
import collections
import ctypes as C
import gzip
import json
import pathlib
import time
import numpy as np
import build_entity_bc_dataset as dataset
import entity_bc_labels as labels
import multi_bc_labels as multi
import replay_native as native

OP = {v:k for k,v in native.UNIT_OPS.items()}


def units(action):
    return [action.farmer, *action.hands[:action.hand_count]]


def work(action):
    return collections.Counter((c.op,c.arg if c.op == 7 else -1)
                               for c in units(action) if c.op in (7,10,11,12,13))


def audit(lib, entry, tape, seat, profile, examples):
    cfg = native.replay_config(tape)
    ctx = lib.kag_bc_create(C.byref(cfg),str(profile).encode())
    if not ctx: raise RuntimeError('allocation failed')
    obs = np.empty(labels.OBS,np.float32)
    mask = np.empty(labels.MASK,np.uint8)
    positions = np.empty((native.KG_MAX_HANDS+1,2),np.int32)
    facts = np.empty((native.KG_MAX_HANDS+1,4),np.int32)
    rewards = np.empty(2,np.float32)
    changed_units = np.empty(native.KG_MAX_HANDS+1,np.int32)
    fills = np.empty(native.KG_MAX_MARKET_ORDERS,np.int32)
    counts = collections.Counter()
    first_prefix = []
    def decode(heads):
        a = native.CAction()
        if not lib.kag_bc_decode(ctx,seat,heads.ctypes.data,C.byref(a)): raise RuntimeError('decode')
        return a
    def teacher_mask(heads,output):
        if not lib.kag_bc_teacher_mask(ctx,seat,heads.ctypes.data,output.ctypes.data): raise RuntimeError('mask')
        if not first_prefix: first_prefix.append((heads.copy(),output.copy()))
    def work_facts(action):
        out = np.empty_like(facts)
        n = lib.kag_bc_work_facts(ctx,seat,C.byref(action),out.ctypes.data,out.size)
        if n < 1: raise RuntimeError('work facts failed')
        return out[:n]
    try:
        for step, raw_pair in enumerate(tape['actions']):
            pair = (native.CAction*2)(*(native.c_action(a) for a in raw_pair))
            lib.kag_bc_view(ctx,seat,obs.ctypes.data,mask.ctypes.data)
            n = lib.kag_bc_positions(ctx,seat,positions.ctypes.data,positions.size)
            first_prefix.clear()
            lib.kag_bc_work_facts(ctx,seat,C.byref(pair[seat]),facts.ctypes.data,facts.size)
            result,history,report = multi.project(pair[seat],mask,decode,positions[:n],teacher_mask,cfg.board_size,facts[:n],work_facts)
            counts.update(report)
            counts['turns'] += 1
            counts['market_nonempty'] += pair[seat].market_count > 0
            counts['market_nonempty_exact'] += pair[seat].market_count > 0 and report['market_queue_exact_label']
            preview = decode(history)
            preview_stats = collections.Counter()
            expected,_ = multi.observed_groups(pair[seat],positions[:n],facts[:n],cfg.board_size,collections.Counter())
            actual,_ = multi.observed_groups(preview,positions[:n],work_facts(preview),cfg.board_size,preview_stats)
            counts['effective_strategy_rows'] += bool(expected)
            counts['effective_strategy_region_exact'] += bool(expected) and expected == actual
            production = {k:v for k,v in expected.items() if k[0] <= 9}
            actual_production = {k:v for k,v in actual.items() if k[0] <= 9}
            counts['effective_production_rows'] += bool(production)
            counts['effective_production_region_exact'] += bool(production) and production == actual_production
            harvest = {k:v for k,v in expected.items() if 11 <= k[0] < 16}
            actual_harvest = {k:v for k,v in actual.items() if 11 <= k[0] < 16}
            counts['crop_harvest_rows'] += bool(harvest)
            counts['crop_harvest_region_exact'] += bool(harvest) and harvest == actual_harvest
            if hasattr(lib,'kag_bc_effects'):
                lib.kag_bc_effects(ctx,seat,pair,changed_units.ctypes.data,fills.ctypes.data,changed_units.size)
                for u,cmd in enumerate(units(pair[seat])):
                    if cmd.op in (7,10,11,12,13) and not changed_units[u]: counts[f'ineffective_{OP[cmd.op]}'] += 1
                counts['filled_market_orders'] += int(np.count_nonzero(fills[:pair[seat].market_count] > 0))
            wanted, got = work(pair[seat]), work(preview)
            for (op,arg), amount in wanted.items(): counts[f'requested_{OP[op]}'] += amount
            for (op,arg), amount in (wanted-got).items(): counts[f'missing_{OP[op]}'] += amount
            for (op,arg), amount in (got-wanted).items(): counts[f'extra_{OP[op]}'] += amount
            for slot in range(pair[seat].market_count):
                if not report.get(f'market_conflict_slot_{slot}'): continue
                command = multi.market_command(pair[seat].market[slot])
                if hasattr(lib,'kag_bc_effects'):
                    requested = 1 if command in (19,20) else pair[seat].market[slot].n
                    kind = 'zero_fill' if fills[slot] == 0 else 'partial_fill' if fills[slot] < requested else 'full_fill'
                    counts[f'market_first_conflict_{kind}'] += 1
                    counts[f'market_first_conflict_{kind}_command_{command}'] += 1
                break
            category = 'production' if wanted != got else 'market' if report.get('market_mask_conflict') else None
            if category and sum(x['category'] == category for x in examples) < 30:
                state = native.c_snapshot(lib,lib.kag_bc_state(ctx))
                farm, private = state['farms'][seat], state['privates'][seat]
                changed = []
                for u,(expected,actual) in enumerate(zip(units(pair[seat]),units(preview))):
                    if expected.op == actual.op and expected.arg == actual.arg: continue
                    if u >= n: continue
                    x,y = map(int,positions[u])
                    changed.append(dict(worker=u,xy=[x,y],expected=[OP[expected.op],expected.arg,expected.n],
                        actual=[OP[actual.op],actual.arg,actual.n],tile=farm['tiles'][y][x],
                        inventory=private['inventories'][u]))
                examples.append(dict(category=category,episode=entry['episode_id'],step=step,seat=seat,
                    name=tape['info']['TeamNames'][seat],money=farm['money'],day=state['day'],hour=state['hour'],
                    shed=private['shed'],seeds=private['seeds'],heads=history.tolist(),
                    raw=raw_pair[seat],changed=changed,report=dict(report)))
            if not lib.kag_bc_step(ctx,pair,seat,history.ctypes.data,rewards.ctypes.data): raise RuntimeError('step')
        actual_cash = [lib.kg_player_money(lib.kag_bc_state(ctx),p) for p in range(2)]
        if actual_cash != tape['rewards']: raise ValueError('replay cash mismatch')
        return dict(episode=entry['episode_id'],split=entry['split'],seat=seat,
                    name=tape['info']['TeamNames'][seat],counts=dict(counts))
    finally:
        lib.kag_bc_destroy(ctx)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=pathlib.Path,required=True)
    parser.add_argument('--profile',type=pathlib.Path,required=True)
    parser.add_argument('--lib',type=pathlib.Path,required=True)
    parser.add_argument('--teacher',default='Majkel1337',help='Exact name or ALL for both seats')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args = parser.parse_args()
    if args.output.exists(): parser.error('choose a new output path')
    manifest = json.loads(args.manifest.read_text())
    lib = dataset.load_bridge(args.lib.resolve())
    start = time.monotonic(); records = []; examples = []
    for entry in manifest['cache']:
        with gzip.open(entry['path'],'rt') as stream: tape = json.load(stream)
        for seat,name in enumerate(tape['info']['TeamNames']):
            if args.teacher not in ('ALL',name): continue
            records.append(audit(lib,entry,tape,seat,args.profile.resolve(),examples))
    total = collections.Counter()
    for record in records: total.update(record['counts'])
    result = dict(source_hash=f'{lib.kag_bc_source_hash():016x}',teacher=args.teacher,
                  wall_seconds=time.monotonic()-start,totals=dict(total),records=records,examples=examples)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    encoded = json.dumps(result,indent=2)
    with args.output.open('x') as stream: stream.write(encoded)
    print(json.dumps({k:v for k,v in result.items() if k not in ('records','examples')},indent=2))


if __name__ == '__main__': main()
