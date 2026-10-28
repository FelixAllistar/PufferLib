#!/usr/bin/env python3
"""Batched evaluate_private.py: the same games (teams, seeds, seats, argmax/sample, 512-update cap) played
--batch at a time, with one Metamon forward per turn for all of them and our CPU policy stepped in threads.

Several checkpoints can share one run (one --output each, in order), e.g. one checkpoint per piloted team, so
the batch stays full. Each output matches evaluate_private.py (games.jsonl, progress.txt, result.txt,
summary.json with records in the sequential order) plus metamon_batch. Per game the computation is the same as
evaluate_private.py: --batch 1 reproduces it exactly, and larger batches only differ by last-bit GEMM rounding
(142/144 identical games, same score, in the 2026-10-25 check).

--mirror (with --team-dir): a fair team-for-team test. For every ordered pair of teams (X, Y) our policy pilots X
(its exact sets, drafted under forced masks while the policy runs) and Metamon pilots Y; (X, Y) and (Y, X) share a
seed and seat, so both sides get the same pairings and luck. X == Y games are true mirror matches.
--games is then the number of seeds per ordered pair (seats alternate across seeds).
"""
import argparse
import ctypes as C
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import time

from evaluate_private import PANEL, bind_parent, write_native_progress, sha, load_team, library
from private_view import PublicMechanics, ROOT


def report(args, run, opponent, source_hashes):
    import metamon,torch
    from metamon_opponent import MODELS
    records=[run['records'][i] for i in range(len(run['jobs']))]
    wins=sum(r['score']==1 for r in records);draws=sum(r['score']==.5 for r in records)
    window=MODELS[args.opponent]['window']
    checkpoint=run['checkpoint']
    return dict(checkpoint=str(checkpoint),sha256=sha(checkpoint),config_sha256=sha(checkpoint.with_name('config.ini')),
        opponent=args.opponent,mode=args.mode,information='Both players use native private player observations; no full-state search',
        protocol='native-private-v2'+('-mirror' if args.mirror else ''),metamon_batch=opponent.batch,games=len(records),
        mirror_score=(lambda m:sum(r['score'] for r in m)/len(m) if m else None)([r for r in records if r.get('ours')==r['team']]),games_per_team=args.games,seed=args.seed,
        team_set=str(args.team_dir.resolve()) if args.team_dir else 'panel',
        wins=wins,draws=draws,losses=len(records)-wins-draws,score=(wins+.5*draws)/len(records),
        timeouts=sum(r['timeout'] for r in records),elapsed_seconds=time.monotonic()-run['started'],
        adapter_sha256=sha(args.library),records=records,source_sha256=source_hashes,
        limitations=['Native engine and 512-update draw cap, not a Showdown ladder rating.',
                     'Compact private observation projection requires qualification before model-strength claims.',
                     'Public move PP and effect order are reconstructed from compact observations, not a complete Showdown event stream.',
                     'Batched Metamon forward: same per-game computation, but last-bit GEMM rounding can change rare sampled actions.'],
        metamon_model=args.opponent,metamon_version=metamon.__version__,torch_version=torch.__version__,
        metamon_checkpoint=opponent.maker.default_checkpoint,metamon_action_mode='sample',
        metamon_checkpoint_sha256=opponent.checkpoint_sha256,metamon_device=str(opponent.device),
        metamon_revision='0a00a759c9a4382a2877088d828302ec294a05a5',
        metamon_final_mask='Condition clipped published probabilities on public legal actions; no invalid-action fallback',
        metamon_attention=f'causal torch SDPA window={"full" if window is None else f"({window},0)"}, BF16 Q/K/V rounding')


def main():
    # PK_EVAL_GPU_FRACTION (e.g. 0.45): cap this process's share of GPU memory. Alone on the GPU, PyTorch's cache otherwise
    # grows to fill it, and a training run started next to the eval cannot allocate.
    fraction=float(os.environ.get('PK_EVAL_GPU_FRACTION','0'))
    if fraction>0:
        import torch
        if torch.cuda.is_available():torch.cuda.set_per_process_memory_fraction(fraction)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint',type=Path,nargs='+')
    parser.add_argument('--opponent',choices=['TaurosV0','SyntheticRLV2','SmallRL','MediumRL','LargeRL','Kakuna','Abra'],required=True)
    parser.add_argument('--mode',choices=['argmax','sample'],default='argmax')
    parser.add_argument('--team',choices=PANEL,action='append')
    parser.add_argument('--team-dir',type=Path)
    parser.add_argument('--games',type=int,default=128,help='Per team, alternating seats')
    parser.add_argument('--seed',type=int,default=20261018)
    parser.add_argument('--batch',type=int,default=256,help='Concurrent games (Metamon batch size)')
    parser.add_argument('--workers',type=int,default=8,help='Threads stepping our CPU policy')
    parser.add_argument('--threads',type=int,default=2,help='torch CPU threads')
    parser.add_argument('--device',choices=['cpu','cuda'],default='cuda')
    parser.add_argument('--output',type=Path,nargs='+',required=True,help='One per checkpoint')
    parser.add_argument('--mirror',action='store_true',help='Fair team-for-team test over --team-dir (see above)')
    parser.add_argument('--skip-done',action='store_true',help='Skip outputs that already have summary.json')
    parser.add_argument('--library',type=Path,default=ROOT/'build/pokemon/private_adapter.so')
    parser.add_argument('--parent-pid',type=int,default=0)
    args=parser.parse_args()
    bind_parent(args.parent_pid)
    if args.games<2 or args.games%2:parser.error('games must be positive and even')
    if args.mirror and not args.team_dir:parser.error('--mirror needs --team-dir')
    if args.team_dir and args.team:parser.error('--team and --team-dir are exclusive')
    if len(args.output)!=len(args.checkpoint):parser.error('give one --output per checkpoint')
    team_paths={name:None for name in args.team or PANEL}
    if args.team_dir:
        team_paths={path.name.split('.')[0]:path for path in sorted(args.team_dir.resolve(strict=True).iterdir())
                    if path.name.endswith(('_team','.paste.txt'))}
        if not team_paths:parser.error('--team-dir has no team files')
    source_hashes={name:sha(Path(__file__).with_name(name)) for name in
                  ['evaluate_batched.py','metamon_batched.py','evaluate_private.py','private_view.py',
                   'metamon_opponent.py','private_adapter.c','adapter.c']}
    rules=PublicMechanics()
    teams=[(name,*load_team(name,rules,path)) for name,path in team_paths.items()]
    runs=[]
    for checkpoint,output in zip(args.checkpoint,args.output):
        if args.skip_done and (output/'summary.json').exists():continue
        output.mkdir(parents=True,exist_ok=False)
        if args.mirror:
            index={name:i for i,(name,_,_) in enumerate(teams)}
            pair=lambda x,y:min(index[x],index[y])*len(teams)+max(index[x],index[y])
            jobs=[dict(team=y,ours=x,team_sha256=ysha,array=yteam,ours_array=xteam,game=game,seat=game%2,
                       seed=(args.seed+(pair(x,y)*args.games+game)*0x9e3779b97f4a7c15)%(1<<64))
                  for x,xteam,_ in teams for y,yteam,ysha in teams for game in range(args.games)]
        else:
            jobs=[dict(team=name,team_sha256=team_sha,array=team,game=game,seat=game%2,
                       seed=(args.seed+game*0x9e3779b97f4a7c15)%(1<<64)) for name,team,team_sha in teams for game in range(args.games)]
        runs.append(dict(checkpoint=checkpoint.resolve(strict=True),output=output,jobs=jobs,records={},left=len(jobs),
                         free=[],started=None))
        write_native_progress(output,[],len(jobs))
    queue=iter([(run,i) for run in runs for i in range(len(run['jobs']))])
    total=sum(len(run['jobs']) for run in runs)
    if not total:return
    batch=min(args.batch,total)
    lib=library(args.library)
    if args.mirror:
        lib.private_reset_mirror.argtypes=[C.c_void_p,C.c_uint64,C.c_int,C.c_void_p,C.c_void_p]
        lib.private_reset_mirror.restype=C.c_int
    os.environ.setdefault('METAMON_CACHE_DIR',str(ROOT/'build/pokemon/metamon-cache'))
    from metamon_batched import BatchedMetamon
    opponent=BatchedMetamon(args.opponent,batch,threads=args.threads,device=args.device)
    pool=ThreadPoolExecutor(args.workers)
    started=time.monotonic();finished=0;live={}
    def refill(slots):
        """Start the next queued game in each free slot; handles (policy copies) are reused per checkpoint."""
        starts=[]
        for slot in slots:
            item=next(queue,None)
            if item is None:break
            run,index=item
            if run['started'] is None:run['started']=time.monotonic()
            handle=run['free'].pop() if run['free'] else None
            starts.append((slot,run,index,handle))
        def reset(start):
            slot,run,index,handle=start;job=run['jobs'][index]
            if handle is None:handle=lib.bench_create(str(run['checkpoint']).encode(),args.mode=='argmax')
            result=(lib.private_reset_mirror(handle,job['seed'],job['seat'],job['ours_array'],job['array']) if 'ours' in job
                    else lib.bench_reset(handle,job['seed'],job['seat'],job['array']))
            if result:raise RuntimeError(f'Invalid reset: {result}')
            return handle
        for (slot,run,index,_),handle in zip(starts,pool.map(reset,starts)):
            opponent.reset(slot,run['jobs'][index]['seed'])
            live[slot]=dict(run=run,index=index,handle=handle,step=0,digest=hashlib.sha256())
    def finish(slot,result):
        nonlocal finished
        game=live.pop(slot);run=game['run'];job=run['jobs'][game['index']]
        winner=result-1 if result in (1,2) else None
        run['records'][game['index']]=record=dict(team=job['team'],**({'ours':job['ours']} if 'ours' in job else {}),
            team_sha256=job['team_sha256'],game=job['game'],
            seat=job['seat'],seed=job['seed'],score=.5 if winner is None else float(winner==job['seat']),
            timeout=result==5,decisions=game['step'],opponent_observation_sha256=game['digest'].hexdigest())
        with (run['output']/'games.jsonl').open('a') as stream:stream.write(json.dumps(record)+'\n')
        write_native_progress(run['output'],list(run['records'].values()),len(run['jobs']))
        run['left']-=1;run['free'].append(game['handle']);finished+=1
        if not run['left']:
            for handle in run['free']:lib.bench_destroy(handle)
            run['free']=[]
            summary=report(args,run,opponent,source_hashes)
            (run['output']/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
            write_native_progress(run['output'],summary['records'],len(run['jobs']),final=True)
            print(f'{run["output"]} {args.opponent} {summary["score"]:.3f}',flush=True)
        if finished%64==0 or finished==total:
            print(f'{args.opponent} {args.mode} batch={batch} {finished}/{total} '
                  f'{finished/(time.monotonic()-started)*60:.1f} games/min',flush=True)
    try:
        refill(range(batch))
        buffers=[(C.c_uint8*648)() for _ in range(batch)]
        while live:
            observations={}
            for slot,game in live.items():
                if lib.private_observation(game['handle'],buffers[slot]):raise RuntimeError('Unexpected terminal observation')
                observations[slot]=bytes(buffers[slot]);game['digest'].update(observations[slot])
            actions=opponent.choose(observations)
            for slot,action in actions.items():
                if not observations[slot][480+action]:raise RuntimeError('Opponent selected an illegal action')
            slots=list(actions);ended=[]
            for slot,result in zip(slots,pool.map(lambda s:lib.private_step(live[s]['handle'],actions[s]),slots)):
                if result<0 or result==4:raise RuntimeError(f'Native engine error: {result}')
                live[slot]['step']+=1
                if result:finish(slot,result);ended.append(slot)
            refill(ended)
    finally:
        pool.shutdown()
        for game in live.values():lib.bench_destroy(game['handle'])
        for run in runs:
            for handle in run['free']:lib.bench_destroy(handle)


if __name__=='__main__':main()
