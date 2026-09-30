"""Exhaustive legacy 2/1 coverage on parity-checked official replay states.

This is a teacher-forced capacity audit, not a closed-loop strategy evaluator.
Market coverage means exact requested orders, not equivalent economic fills.
"""
import argparse
import collections
import ctypes as C
import json
import pathlib
import sys
import time
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import prepare_bc_replays as prep
import replay_native as native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=pathlib.Path, required=True)
    parser.add_argument('--episode', required=True)
    parser.add_argument('--seat', type=int, required=True, choices=(0, 1))
    parser.add_argument('--lib', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--current-lib', type=pathlib.Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output already exists')
    with zipfile.ZipFile(args.archive) as archive:
        episode = json.loads(archive.read(args.episode + '.json'))
    tape = prep.compact_tape(episode)
    lib = native.load_core(args.lib.resolve())
    prep.verify_tape(lib, tape, episode['steps'])
    print('PASS: all official replay frames and terminal cash match', flush=True)
    lib.audit_exec21.argtypes = [C.c_void_p, C.c_int, C.POINTER(native.CAction),
                                C.POINTER(C.c_int)]
    lib.audit_exec21.restype = None
    cfg = native.replay_config(tape)
    state = lib.kg_create(C.byref(cfg))
    context = None
    if args.current_lib:
        import numpy as np
        import build_entity_bc_dataset as dataset
        import entity_bc_labels as labels
        import multi_bc_labels as multi
        current = dataset.load_bridge(args.current_lib.resolve())
        prep.verify_tape(current, tape, episode['steps'])
        context = current.kag_bc_create(C.byref(cfg),
            b'ocean/kaggriculture/profiles/terminal.ini')
        obs = np.empty(labels.OBS, np.float32)
        mask = np.empty(labels.MASK, np.uint8)
        positions = np.empty((native.KG_MAX_HANDS + 1, 2), np.int32)
        facts = np.empty((native.KG_MAX_HANDS + 1, 4), np.int32)
        reward = np.empty(2, np.float32)
        lib.audit_measure.argtypes = [C.c_void_p, C.c_int,
            C.POINTER(native.CAction), C.POINTER(native.CAction), C.POINTER(C.c_int)]
        def decode(heads):
            action = native.CAction()
            assert current.kag_bc_decode(context, args.seat, heads.ctypes.data, C.byref(action))
            return action
        def teacher_mask(heads, output):
            assert current.kag_bc_teacher_mask(context, args.seat,
                heads.ctypes.data, output.ctypes.data)
        def work_facts(action):
            output = np.empty_like(facts)
            count = current.kag_bc_work_facts(context, args.seat,
                C.byref(action), output.ctypes.data, output.size)
            assert count > 0
            return output[:count]
    total = collections.Counter()
    records = []
    start = time.monotonic()
    try:
        for turn, raw in enumerate(tape['actions']):
            pair = (native.CAction * 2)(*(native.c_action(a) for a in raw))
            out = (C.c_int * 9)()
            lib.audit_exec21(state, args.seat, C.byref(pair[args.seat]), out)
            wanted, matched, extra, candidates, exact, combined, macro, q, region = out
            total['turns'] += 1
            total['candidates'] += candidates
            total['successful_strategic_effects'] += wanted
            total['best_matched_effects'] += matched
            total['best_extra_effects'] += extra
            total['strategic_turns'] += wanted > 0
            total['exact_strategic_turns'] += wanted > 0 and bool(exact)
            total['exact_work_all_turns'] += bool(exact)
            total['exact_work_and_market_commands'] += bool(combined)
            records.append(dict(turn=turn, wanted=wanted, matched=matched, extra=extra,
                                exact=bool(exact), exact_work_market=bool(combined),
                                best_request=[macro, q, region]))
            if context:
                assert current.kag_bc_view(context, args.seat, obs.ctypes.data, mask.ctypes.data)
                count = current.kag_bc_positions(context, args.seat,
                    positions.ctypes.data, positions.size)
                teacher_facts = work_facts(pair[args.seat])
                _, history, report = multi.project(pair[args.seat], mask, decode,
                    positions[:count], teacher_mask, cfg.board_size, teacher_facts, work_facts)
                preview = decode(history)
                measured = (C.c_int * 3)()
                lib.audit_measure(state, args.seat, C.byref(pair[args.seat]),
                    C.byref(preview), measured)
                assert measured[0] == wanted
                total['current_matched_effects'] += measured[1]
                total['current_extra_effects'] += measured[2]
                total['current_exact_strategic_turns'] += wanted > 0 and (
                    measured[1] == wanted and measured[2] == 0)
                records[-1]['current_effects'] = list(measured)
                records[-1]['current_projection_report'] = dict(report)
                assert current.kag_bc_step(context, pair, args.seat,
                    history.ctypes.data, reward.ctypes.data)
            lib.kg_step(state, pair)
            if (turn + 1) % 100 == 0:
                print(json.dumps(dict(turn=turn + 1, seconds=time.monotonic()-start,
                                      totals=dict(total))), flush=True)
    finally:
        lib.kg_destroy(state)
        if context:
            current.kag_bc_destroy(context)
    result = dict(legacy_commit='6abfb8dd4', archive=str(args.archive),
                  episode=args.episode, seat=args.seat,
                  player=episode['info']['TeamNames'][args.seat],
                  final_cash=episode['rewards'][args.seat],
                  parity='all frames and terminal cash exact',
                  method='teacher-forced exhaustive legal 2/1 search; unlimited legacy hands',
                  limitations=['No closed-loop rollout or rescheduling.',
                               'Current 2/2 uses BC projection, not exhaustive search.',
                               'Market metric compares commands, not fills.',
                               'Strategic metric excludes harvest and maintenance.',
                               'Best request maximizes matched effects, then minimizes extras.'],
                  totals=dict(total), seconds=time.monotonic()-start, records=records)
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps({k: v for k, v in result.items() if k != 'records'}, indent=2))


if __name__ == '__main__':
    main()
