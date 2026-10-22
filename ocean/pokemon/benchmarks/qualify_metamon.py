#!/usr/bin/env python3
"""Qualify a released Metamon model on the native private projection.

Plays short games (our checkpoint vs the model) and checks, per Metamon decision:
- the tokenizer recognises every projected word (unknown words are listed);
- our BF16 SDPA attention backend agrees with upstream VanillaAttention on the
  same weights and observation stream (probability gap, argmax agreement);
- no illegal action or native engine error occurs.
Strength sanity is a separate step: compare weak and strong checkpoints.
"""
import argparse
import collections
import copy
import ctypes as C
import json
import os
from pathlib import Path
import sys

os.environ.setdefault('TORCH_COMPILE_DISABLE','1')
sys.path.insert(0,str(Path(__file__).resolve().parent))
from private_view import PublicMechanics, ROOT
os.environ.setdefault('METAMON_CACHE_DIR',str(ROOT/'build/pokemon/metamon-cache'))

import torch
from amago.nets.transformer import VanillaAttention
from metamon.tokenizer.tokenizer import PokemonTokenizer, UNKNOWN_TOKEN
from evaluate_private import PANEL, library, load_team
from metamon_opponent import MetamonOpponent, NativeWindowAttention


def upstream_attention_twin(policy):
    twin=copy.deepcopy(policy)
    replaced=0
    for module in list(twin.modules()):
        for name,child in list(module.named_children()):
            if isinstance(child,NativeWindowAttention):
                if child.window is not None:
                    raise ValueError('VanillaAttention has no window; only full-causal models compare')
                setattr(module,name,VanillaAttention(causal=True,dropout=0.));replaced+=1
    if not replaced:raise ValueError('No attention modules found')
    return twin.eval(),replaced


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint',type=Path)
    parser.add_argument('--model',default='SyntheticRLV2')
    parser.add_argument('--games',type=int,default=2,help='Per panel team')
    parser.add_argument('--seed',type=int,default=20261020)
    parser.add_argument('--device',choices=['cpu','cuda'],default='cuda')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    unknown=collections.Counter()
    lookup=PokemonTokenizer.__getitem__
    def logged(self,string):
        token=lookup(self,string)
        if token==UNKNOWN_TOKEN:unknown[string]+=1
        return token
    PokemonTokenizer.__getitem__=logged
    opponent=MetamonOpponent(name=args.model,sample=True,device=args.device)
    # Windowed models (TaurosV0) have no upstream torch twin; they still get
    # the vocabulary and legality checks, as a baseline for the others.
    windowed=any(isinstance(m,NativeWindowAttention) and m.window is not None for m in opponent.policy.modules())
    twin,replaced=(None,0) if windowed else upstream_attention_twin(opponent.policy)
    shadow=dict(policy=twin,log=[])
    if twin is not None:opponent.shadows.append(shadow)
    decisions=[0]
    choose=opponent.choose
    def counted(observation):
        action=choose(observation)
        if not (observation[6]==0 and observation[490]):decisions[0]+=1
        return action
    opponent.choose=counted
    rules=PublicMechanics();lib=library(ROOT/'build/pokemon/private_adapter.so')
    handle=lib.bench_create(str(args.checkpoint.resolve(strict=True)).encode(),1)
    games=[];lengths=[]
    try:
        for team_name in PANEL:
            team,_=load_team(team_name,rules)
            for game in range(args.games):
                seat=game%2;seed=(args.seed+game*0x9e3779b97f4a7c15)%(1<<64)
                result=lib.bench_reset(handle,seed,seat,team)
                if result:raise RuntimeError(f'Invalid reset: {result}')
                opponent.reset(seed);start=decisions[0];step=0
                while not result:
                    buffer=(C.c_uint8*648)()
                    if lib.private_observation(handle,buffer):raise RuntimeError('Unexpected terminal observation')
                    observation=bytes(buffer)
                    action=opponent.choose(observation)
                    if not observation[480+action]:raise RuntimeError('Opponent selected an illegal action')
                    result=lib.private_step(handle,action)
                    if result<0 or result==4:raise RuntimeError(f'Native engine error: {result}')
                    step+=1
                winner=result-1 if result in (1,2) else None
                games.append(dict(team=team_name,seat=seat,decisions=step,metamon_decisions=decisions[0]-start,
                                  learner_score=.5 if winner is None else float(winner==seat)))
                lengths.append(decisions[0]-start)
                print(json.dumps(games[-1]),flush=True)
    finally:
        lib.bench_destroy(handle)
    gaps=[float((a-b).abs().max()) for a,b in shadow['log']] or [float('nan')]
    agree=[int(a.argmax())==int(b.argmax()) for a,b in shadow['log']] or [float('nan')]
    context=getattr(getattr(opponent.policy,'traj_encoder',None),'max_seq_len',None)
    report=dict(model=args.model,checkpoint=str(args.checkpoint),sha256=opponent.checkpoint_sha256,
        attention_modules_compared=replaced,games=games,metamon_decisions=decisions[0],
        longest_game_metamon_decisions=max(lengths),
        context_length=context,context_rollover_exercised=None if context is None else max(lengths)>context,
        probability_gap_max=max(gaps),probability_gap_mean=sum(gaps)/len(gaps),
        argmax_agreement=sum(agree)/len(agree),
        unknown_words=dict(unknown.most_common()),unknown_lookups=sum(unknown.values()))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='games'}),flush=True)


if __name__=='__main__':main()
