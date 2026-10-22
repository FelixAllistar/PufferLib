#!/usr/bin/env python3
"""Native Gen1 player-information benchmark; deliberately independent of Oak."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import sys
import time

from private_view import PublicMechanics, ROOT, heuristic_action, normalize

PANEL=('zam_egg_mie','jynx_mie_don','gar_egg_zap')


def bind_parent(parent_pid):
    """Native trainer owns this evaluator; never leave an orphan GPU process."""
    if not parent_pid:
        return
    if sys.platform.startswith('linux'):
        libc=C.CDLL(None,use_errno=True)
        if libc.prctl(1,signal.SIGTERM,0,0,0):  # PR_SET_PDEATHSIG
            raise OSError(C.get_errno(),'Cannot bind evaluator lifetime to trainer')
    if os.getppid()!=parent_pid:
        raise RuntimeError('Trainer already exited; evaluation cancelled')


def write_native_progress(output, records, total, final=False):
    wins=sum(row['score']==1 for row in records)
    draws=sum(row['score']==.5 for row in records)
    n=len(records)
    score=(wins+.5*draws)/n if n else 0
    name='result.txt' if final else 'progress.txt'
    path=output/name
    temporary=output/(name+'.tmp')
    temporary.write_text(f'METAMON_V1 {n} {wins} {draws} {n-wins-draws} {score:.17g} {total}\n')
    temporary.replace(path)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_team(name, rules, path=None):
    """Showdown export; Gen1 has no items/abilities and EV/IV lines are ignored."""
    path=path or ROOT/'reports/pokemon_benchmarks/20261014_all_roster'/f'{name}.paste.txt'
    species={normalize(p['name']):p['id'] for p in rules.data['species']}
    moves={normalize(m['name']):m['id'] for m in rules.data['moves']}
    team=[]
    for block in path.read_text().strip().split('\n\n'):
        lines=[line.strip() for line in block.splitlines()]
        chosen=[moves[normalize(line[2:])] for line in lines if line.startswith('- ')]
        if len(chosen)!=4:raise ValueError('Expected four pinned moves')
        species_name=re.sub(r' \([MF]\)$','',re.sub(r' @ .*$','',lines[0]))
        nickname=re.fullmatch(r'.*\((.+)\)',species_name)
        if nickname:species_name=nickname.group(1)
        team.append([species[normalize(species_name)],*chosen])
    if len(team)!=6:raise ValueError('Expected six pinned Pokemon')
    return (C.c_uint8*30)(*[v for mon in team for v in mon]),sha(path)


def library(path):
    lib=C.CDLL(str(path.resolve()))
    definitions={
        'bench_create':([C.c_char_p,C.c_int],C.c_void_p),
        'bench_reset':([C.c_void_p,C.c_uint64,C.c_int,C.c_void_p],C.c_int),
        'private_observation':([C.c_void_p,C.c_void_p],C.c_int),
        'private_step':([C.c_void_p,C.c_int],C.c_int),
        'bench_destroy':([C.c_void_p],None),
    }
    for name,(arguments,result) in definitions.items():
        fn=getattr(lib,name);fn.argtypes=arguments;fn.restype=result
    return lib


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint',type=Path)
    parser.add_argument('--opponent',choices=['heuristic','TaurosV0','SyntheticRLV2','SmallRL','MediumRL','LargeRL'],required=True,
                        help='Choose explicitly: heuristic is only our own adapter sanity check')
    parser.add_argument('--mode',choices=['argmax','sample'],default='argmax')
    parser.add_argument('--team',choices=PANEL,action='append')
    parser.add_argument('--team-dir',type=Path,help='Opponent team set: every Showdown export in this directory, '
                        'e.g. a pinned copy of Metamon gen1ou "competitive"; replaces the panel')
    parser.add_argument('--games',type=int,default=128,help='Per team, alternating seats')
    parser.add_argument('--seed',type=int,default=20261018)
    parser.add_argument('--threads',type=int,default=2)
    parser.add_argument('--device',choices=['cpu','cuda'],default='cpu',help='Metamon inference device; CUDA never silently falls back')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--library',type=Path,default=ROOT/'build/pokemon/private_adapter.so')
    parser.add_argument('--parent-pid',type=int,default=0,help='Native trainer lifetime guard')
    args=parser.parse_args()
    bind_parent(args.parent_pid)
    if args.games<2 or args.games%2:parser.error('games must be positive and even')
    if args.team_dir and args.team:parser.error('--team and --team-dir are exclusive')
    team_paths={name:None for name in args.team or PANEL}
    if args.team_dir:
        team_paths={path.name.split('.')[0]:path for path in sorted(args.team_dir.resolve(strict=True).iterdir())
                    if path.name.endswith(('_team','.paste.txt'))}
        if not team_paths:parser.error('--team-dir has no team files')
    checkpoint=args.checkpoint.resolve(strict=True)
    source_hashes={name:sha(Path(__file__).with_name(name)) for name in
                  ['evaluate_private.py','private_view.py','metamon_opponent.py','private_adapter.c','adapter.c']}
    args.output.mkdir(parents=True,exist_ok=False)
    total=args.games*len(team_paths)
    write_native_progress(args.output,[],total)
    rules=PublicMechanics()
    lib=library(args.library)
    opponent=None
    if args.opponent!='heuristic':
        os.environ.setdefault('METAMON_CACHE_DIR',str(ROOT/'build/pokemon/metamon-cache'))
        from metamon_opponent import MetamonOpponent
        opponent=MetamonOpponent(name=args.opponent,threads=args.threads,device=args.device)
    handle=lib.bench_create(str(checkpoint).encode(),args.mode=='argmax')
    records=[];started=time.monotonic()
    try:
        for team_name,team_path in team_paths.items():
            team,team_sha=load_team(team_name,rules,team_path)
            for game in range(args.games):
                seat=game%2
                seed=(args.seed+game*0x9e3779b97f4a7c15)%(1<<64)
                result=lib.bench_reset(handle,seed,seat,team)
                if result:raise RuntimeError(f'Invalid reset: {result}')
                if opponent:opponent.reset(seed)
                step=0;digest=hashlib.sha256()
                while not result:
                    buffer=(C.c_uint8*648)()
                    if lib.private_observation(handle,buffer):raise RuntimeError('Unexpected terminal observation')
                    observation=bytes(buffer)
                    digest.update(observation)
                    action=opponent.choose(observation) if opponent else heuristic_action(observation,rules)
                    if not observation[480+action]:raise RuntimeError('Opponent selected an illegal action')
                    result=lib.private_step(handle,action)
                    if result<0 or result==4:raise RuntimeError(f'Native engine error: {result}')
                    step+=1
                winner=result-1 if result in (1,2) else None
                records.append(dict(team=team_name,team_sha256=team_sha,game=game,seat=seat,seed=seed,
                    score=.5 if winner is None else float(winner==seat),timeout=result==5,
                    decisions=step,opponent_observation_sha256=digest.hexdigest()))
                # Preserve completed games if a later game or the process fails.
                with (args.output/'games.jsonl').open('a') as stream:
                    stream.write(json.dumps(records[-1])+'\n')
                write_native_progress(args.output,records,total)
                if (game+1)%8==0 or game+1==args.games:
                    group=[r for r in records if r['team']==team_name]
                    print(f'{args.opponent} {args.mode} {team_name} {game+1}/{args.games}: '
                          f'{sum(r["score"] for r in group)/len(group):.1%}',flush=True)
    finally:
        lib.bench_destroy(handle)
    wins=sum(r['score']==1 for r in records);draws=sum(r['score']==.5 for r in records)
    report=dict(checkpoint=str(checkpoint),sha256=sha(checkpoint),config_sha256=sha(checkpoint.with_name('config.ini')),
        opponent=args.opponent,mode=args.mode,information='Both players use native private player observations; no full-state search',
        protocol='native-private-v2',games=len(records),games_per_team=args.games,seed=args.seed,
        team_set=str(args.team_dir.resolve()) if args.team_dir else 'panel',
        wins=wins,draws=draws,losses=len(records)-wins-draws,score=(wins+.5*draws)/len(records),
        timeouts=sum(r['timeout'] for r in records),elapsed_seconds=time.monotonic()-started,
        adapter_sha256=sha(args.library),records=records,
        source_sha256=source_hashes,
        limitations=['Native engine and 512-update draw cap, not a Showdown ladder rating.',
                     'Compact private observation projection requires qualification before model-strength claims.'])
    if opponent:
        import metamon,torch
        from metamon_opponent import MODELS
        window=MODELS[args.opponent]['window']
        report.update(metamon_model=args.opponent,metamon_version=metamon.__version__,torch_version=torch.__version__,
                      metamon_checkpoint=opponent.maker.default_checkpoint,metamon_action_mode='sample',
                      metamon_checkpoint_sha256=opponent.checkpoint_sha256,
                      metamon_device=str(opponent.device),
                      metamon_revision='0a00a759c9a4382a2877088d828302ec294a05a5',
                      metamon_final_mask='Condition clipped published probabilities on public legal actions; no invalid-action fallback',
                      metamon_attention=f'causal torch SDPA window={"full" if window is None else f"({window},0)"}, BF16 Q/K/V rounding')
        report['limitations'].append('Public move PP and effect order are reconstructed from compact observations, not a complete Showdown event stream.')
    (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    write_native_progress(args.output,records,total,final=True)
    print(json.dumps({k:v for k,v in report.items() if k!='records'}),flush=True)


if __name__=='__main__':main()
