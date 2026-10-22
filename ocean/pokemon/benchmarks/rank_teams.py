#!/usr/bin/env python3
"""Rank candidate teams with control_team.py: TaurosV0 pilots each candidate against TaurosV0 on the
three panel teams. Candidates are Metamon's competitive gen1ou exports plus each league member's
drafted team. Screen with --screen games per panel team, then rerun the top --top at --games."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from private_view import ROOT


def run(checkpoint, team, games, output):
    if (output/'summary.json').exists():
        return json.loads((output/'summary.json').read_text())
    if output.exists():
        return None  # a previous attempt failed; see its log
    command=[sys.executable,str(Path(__file__).with_name('control_team.py')),str(checkpoint),
             '--games',str(games),'--output',str(output)]+(['--learner-team',str(team)] if team else [])
    output.parent.mkdir(parents=True,exist_ok=True)
    log=output.with_suffix('.log')
    with log.open('w') as stream:
        code=subprocess.run(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT).returncode
    return json.loads((output/'summary.json').read_text()) if code==0 else None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint',type=Path,help='Any checkpoint; used only to construct the adapter for file teams')
    parser.add_argument('--league',type=Path,default=ROOT/'leagues/pokemon/rr20m_256x2_20261020/members')
    parser.add_argument('--team-dir',type=Path,default=ROOT/'reports/pokemon_benchmarks/metamon_competitive_gen1ou')
    parser.add_argument('--screen',type=int,default=16)
    parser.add_argument('--games',type=int,default=48)
    parser.add_argument('--top',type=int,default=6)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    candidates={}
    for path in sorted(args.team_dir.glob('*.gen1ou_team')):
        candidates[f'metamon/{path.stem}']=(args.checkpoint.resolve(),path.resolve())
    for member in sorted(p for p in args.league.iterdir() if p.is_dir() and p.name!='master'):
        latest=sorted(d for d in member.iterdir() if (d/'weights.bin').exists())
        if latest:
            candidates[f'league/{member.name}']=((latest[-1]/'weights.bin').resolve(),None)
    results={}
    for name,(checkpoint,team) in candidates.items():
        summary=run(checkpoint,team,args.screen,args.output/'screen'/name.replace('/','__'))
        results[name]=dict(screen=summary['score'] if summary else None)
        print(f'screen {name}: {results[name]["screen"]}',flush=True)
    ranked=sorted((n for n in results if results[n]['screen'] is not None),key=lambda n:-results[n]['screen'])
    for name in ranked[:args.top]:
        checkpoint,team=candidates[name]
        summary=run(checkpoint,team,args.games,args.output/'full'/name.replace('/','__'))
        if summary:
            results[name].update(full=summary['score'],per_team=summary['per_team'])
        print(f'full {name}: {results[name].get("full")} {results[name].get("per_team")}',flush=True)
    (args.output/'ranking.json').write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__':
    main()
