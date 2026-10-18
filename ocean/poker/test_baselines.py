"""Reference parity, hidden-card isolation, and policy regression probes."""
import ctypes as C
import json
import os
from pathlib import Path
import random
import sys
import time
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
import numpy as np
import eval7
from eval import Native, ROOT, replay, text_card

SOURCE = ROOT / 'build/poker/external/fullhouse-bot'
BUILD = Path(os.environ.get('POKER_BASELINE_BUILD', str(ROOT/'build/poker')))
sys.path.insert(0, str(SOURCE))
from bot import bot
from bot import features as f


def configure(n):
    n.lib.poker_fullhouse_features.argtypes = [C.c_void_p,C.c_int,C.POINTER(C.c_float),C.POINTER(C.c_int32)]
    n.lib.poker_fullhouse_forward.argtypes = [C.POINTER(C.c_float),C.POINTER(C.c_float)]
    n.lib.poker_aggression.argtypes = [C.c_void_p,C.c_int,C.POINTER(C.c_uint32)]
    n.lib.poker_fullhouse_rank.argtypes = [C.POINTER(C.c_uint8), C.c_int]
    n.lib.poker_fullhouse_rank.restype = C.c_uint32


def native_values(n):
    x, amounts, probs = (C.c_float*51)(), (C.c_int32*5)(), (C.c_float*5)()
    n.lib.poker_fullhouse_features(n.storage,0,x,amounts)
    n.lib.poker_fullhouse_forward(x,probs)
    return np.array(x),np.array(amounts),np.array(probs)


def state_dict(n, log):
    s,rows,board = n.snapshot(); p=s[0]
    return {'your_cards':[text_card(c) for c in rows[p][5:7]],
        'community_cards':[text_card(c) for c in board[:s[1]+2]] if s[1] else [],
        'seat_to_act':p,'dealer':s[8],'street':('preflop','flop','turn','river')[s[1]],
        'pot':s[2],'your_stack':rows[p][0],'amount_owed':s[3]-rows[p][3],
        'current_bet':s[3],'your_bet_this_street':rows[p][3],
        'min_raise_to':s[3]+s[4] if any(a>0 for a in n.actions()) else 0,
        'can_check':s[3]==rows[p][3], 'action_log':list(log),
        'players':[{'seat':i,'stack':r[0],'bet_this_street':r[3],
                    'is_folded':not bool(s[5]&(1<<i))} for i,r in enumerate(rows)]}


def main():
    rng=random.Random(2026101701)
    max_feature=max_probability=0.; states=rank_checks=short_checks=privacy=0
    seconds={}
    for players in (2,6):
        n=Native(players=players,library=BUILD/f'libpoker_fullhouse{players}.so'); configure(n)
        for _ in range(1000):
            cs=rng.sample(range(52),rng.choice((5,6,7)))
            got=n.lib.poker_fullhouse_rank((C.c_uint8*len(cs))(*cs),len(cs))
            expected=eval7.evaluate([eval7.Card(text_card(c)) for c in cs])
            assert got==expected,(cs,hex(got),hex(expected))
            rank_checks+=1
        for hand in range(220):
            stack=rng.choice((700,1200,10000,20000))
            deck=rng.sample(range(52),2*players+5); button=hand%players
            n.reset([stack]*players,deck,button)
            sb=button if players==2 else (button+1)%players; bb=(sb+1)%players
            log=[{'action':'small_blind','seat':sb,'amount':50},{'action':'big_blind','seat':bb,'amount':100}]
            history=[]
            while n.snapshot()[0][0]!=255:
                s,rows,_=n.snapshot(); p=s[0]
                state=state_dict(n,log)
                x,amounts,probs=native_values(n)
                ref,mask=f.encode_state_dict(state)
                delta=float(np.max(np.abs(x-ref)))
                assert delta<2e-6,(players,hand,history,np.where(np.abs(x-ref)>2e-6),x,ref)
                max_feature=max(max_feature,delta)
                ref_probs=bot._DEEP_CFR.get_strategy(state)
                delta=float(np.max(np.abs(probs-ref_probs)))
                assert delta<5e-5,(players,hand,delta,probs,ref_probs)
                max_probability=max(max_probability,delta)
                for i in range(5):
                    if not mask[i]: continue
                    move=bot._to_engine_action(i,state)
                    expected=-1 if move['action']=='fold' else move.get('amount',0)
                    if move['action']=='all_in': expected=rows[p][0]+rows[p][3]
                    assert amounts[i]==expected,(i,amounts,move)
                if s[1]==0:
                    pf=bot._push_fold(state)
                    if pf is not None:
                        expected=-1 if pf['action']=='fold' else 0
                        if pf['action']=='all_in' and state['min_raise_to']: expected=rows[p][0]+rows[p][3]
                        assert n.bot(4)==expected,(state,pf,n.bot(4))
                        short_checks+=1
                # Re-deal hidden cards/future board and replay the same public actions.
                if states%41==0:
                    private=[i for i in range(2*players+5) if i not in (2*p,2*p+1)
                             and not (2*players<=i<2*players+(s[1]+2 if s[1] else 0))]
                    changed=deck.copy(); shuffled=[changed[i] for i in private]; rng.shuffle(shuffled)
                    for i,c in zip(private,shuffled): changed[i]=c
                    other=Native(players=players,library=n.path); configure(other)
                    other.reset([stack]*players,changed,button)
                    for a in history: other.step(a)
                    y,_,q=native_values(other)
                    np.testing.assert_array_equal(x,y); np.testing.assert_array_equal(probs,q)
                    privacy+=1
                action = n.bot(rng.choice((1,4,7)))
                old_street=s[1]
                if action<0: kind,amount='fold',0
                elif action:
                    kind='all_in' if action==rows[p][0]+rows[p][3] else 'raise'; amount=action
                else:
                    kind='call' if s[3]>rows[p][3] else 'check'; amount=min(s[3]-rows[p][3],rows[p][0])
                log.append({'action':kind,'seat':p,'amount':amount})
                history.append(action); n.step(action); states+=1
                after=n.snapshot()[0]
                if after[0]!=255 and after[1]!=old_street: log.append({'action':'deal'})
        # Warm, single-position inference latency; integrated matches timed separately.
        n.reset([20000]*players,rng.sample(range(52),2*players+5))
        state=state_dict(n,[{'action':'small_blind','seat':players-1 if players==2 else 0,'amount':50},
                            {'action':'big_blind','seat':0 if players==2 else 1,'amount':100}])
        start=time.perf_counter()
        for _ in range(5000): n.bot(4)
        native_seconds=time.perf_counter()-start
        start=time.perf_counter()
        for _ in range(1000): bot.decide(state)
        py_seconds=time.perf_counter()-start
        seconds[str(players)]={'native_us_per_decision':native_seconds*1e6/5000,
                              'python_us_per_decision':py_seconds*1e6/1000}
    n=Native(library=BUILD/'libpoker_fullhouse2.so')
    probes=[(['Ah','Kh'],['Qh','7h','2c'],'b250b2000c/b100'),
            (['As','Ad'],['Kh','7c','2d'],'b250c/b500')]
    for holes,board,history in probes:
        replay(n,{'client_pos':1,'hole_cards':holes,'board':board,'action':history})
        assert all(n.bot(1)==-1 for _ in range(1000))
    print(json.dumps({'states':states,'rank_checks':rank_checks,'short_stack_checks':short_checks,
        'hidden_card_checks':privacy,'max_feature_error':max_feature,
        'max_probability_error':max_probability,'latency':seconds},indent=2))


if __name__=='__main__': main()
