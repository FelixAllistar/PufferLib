#!/usr/bin/env python3
"""Team-strength control: a Metamon model pilots OUR fixed learner team (taken from a checkpoint's
config) against the same opponent teams and seeds as evaluate_private.py, with a second instance
of the same model on the other side. Shows how much of our win rate the team itself allows."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import time

from evaluate_private import PANEL, library, load_team, sha
from private_view import PublicMechanics, ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint',type=Path,help='Supplies the learner team and lead (drafted by its policy) unless --learner-team')
    parser.add_argument('--learner-team',type=Path,help='Showdown export for our side instead of the checkpoint team')
    parser.add_argument('--pilot',choices=['TaurosV0','SyntheticRLV2','SmallRL','MediumRL','LargeRL'],default='TaurosV0')
    parser.add_argument('--opponent',choices=['TaurosV0','SyntheticRLV2','SmallRL','MediumRL','LargeRL'],default='TaurosV0')
    parser.add_argument('--games',type=int,default=48,help='Per team, alternating seats')
    parser.add_argument('--seed',type=int,default=20261025)
    parser.add_argument('--device',choices=['cpu','cuda'],default='cuda')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--library',type=Path,default=ROOT/'build/pokemon/private_adapter.so')
    args=parser.parse_args()
    if args.games<2 or args.games%2:parser.error('games must be positive and even')
    args.output.mkdir(parents=True,exist_ok=False)
    rules=PublicMechanics(); lib=library(args.library)
    for name,arguments in (('private_learner_observation',[C.c_void_p,C.c_void_p]),
                           ('private_step_both',[C.c_void_p,C.c_int,C.c_int]),
                           ('private_reset_teams',[C.c_void_p,C.c_uint64,C.c_int,C.c_void_p,C.c_void_p])):
        fn=getattr(lib,name); fn.argtypes=arguments; fn.restype=C.c_int
    os.environ.setdefault('METAMON_CACHE_DIR',str(ROOT/'build/pokemon/metamon-cache'))
    from metamon_opponent import MetamonOpponent
    pilot=MetamonOpponent(name=args.pilot,device=args.device)
    opponent=MetamonOpponent(name=args.opponent,device=args.device)
    handle=lib.bench_create(str(args.checkpoint.resolve(strict=True)).encode(),1)
    ours=load_team(args.learner_team.stem.split('.')[0],rules,args.learner_team)[0] if args.learner_team else None
    records=[]; started=time.monotonic()
    try:
        for team_name in PANEL:
            team,team_sha=load_team(team_name,rules)
            for game in range(args.games):
                seat=game%2
                seed=(args.seed+game*0x9e3779b97f4a7c15)%(1<<64)
                result=(lib.private_reset_teams(handle,seed,seat,ours,team) if ours is not None
                        else lib.bench_reset(handle,seed,seat,team))
                if result:raise RuntimeError(f'Invalid reset: {result}')
                pilot.reset(seed); opponent.reset(seed^0x5bd1e995)
                step=0
                while not result:
                    mine=(C.c_uint8*648)(); theirs=(C.c_uint8*648)()
                    if lib.private_learner_observation(handle,mine) or lib.private_observation(handle,theirs):
                        raise RuntimeError('Unexpected terminal observation')
                    a=pilot.choose(bytes(mine)); b=opponent.choose(bytes(theirs))
                    result=lib.private_step_both(handle,a,b)
                    if result<0 or result==4:raise RuntimeError(f'Native engine error: {result}')
                    step+=1
                winner=result-1 if result in (1,2) else None
                records.append(dict(team=team_name,team_sha256=team_sha,game=game,seat=seat,seed=seed,
                    score=.5 if winner is None else float(winner==seat),timeout=result==5,decisions=step))
                with (args.output/'games.jsonl').open('a') as stream:
                    stream.write(json.dumps(records[-1])+'\n')
                if (game+1)%8==0 or game+1==args.games:
                    group=[r for r in records if r['team']==team_name]
                    print(f'{args.pilot} on our team vs {args.opponent} {team_name} {game+1}/{args.games}: '
                          f'{sum(r["score"] for r in group)/len(group):.1%}',flush=True)
    finally:
        lib.bench_destroy(handle)
    score=sum(r['score'] for r in records)/len(records)
    per_team={t:sum(r['score'] for r in records if r['team']==t)/args.games for t in PANEL}
    report=dict(checkpoint=str(args.checkpoint.resolve()),
        learner_team=str(args.learner_team.resolve()) if args.learner_team else None,pilot=args.pilot,opponent=args.opponent,
        games_per_team=args.games,seed=args.seed,score=score,per_team=per_team,
        wins=sum(r['score']==1 for r in records),draws=sum(r['score']==.5 for r in records),
        losses=sum(r['score']==0 for r in records),elapsed_seconds=time.monotonic()-started,
        source_sha256={n:sha(Path(__file__).with_name(n)) for n in ['control_team.py','private_adapter.c','metamon_opponent.py']})
    (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('score','per_team','wins','draws','losses')}))


if __name__=='__main__':
    main()
