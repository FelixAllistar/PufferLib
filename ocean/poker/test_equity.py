"""Independent river equity oracle, card privacy, and public-profile checks."""
import argparse
import ctypes as C
import itertools
import json
import random
import time
from pathlib import Path

import eval7
from eval import Native, ROOT, card, replay, text_card


class Equity(C.Structure):
    _fields_ = [(k, C.c_float) for k in ('share', 'payout', 'eligible_pot', 'effective_samples')]


class Profile(C.Structure):
    _fields_ = [(k, C.c_uint64) for k in
                ('hands', 'vpip', 'pfr', 'faced', 'folded', 'post_actions', 'post_raises')]
    _fields_ += [('raise_pot_sum', C.c_double)]


def configure(n):
    n.lib.poker_equity.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_uint64),
                                  C.c_int, C.c_int, C.POINTER(Profile), C.POINTER(Equity)]
    n.lib.poker_range_weight.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_uint8,
                                        C.c_uint8, C.POINTER(Profile)]
    n.lib.poker_range_weight.restype = C.c_double
    n.lib.poker_profile_begin.argtypes = [C.POINTER(Profile)]
    n.lib.poker_profile_update.argtypes = [C.POINTER(Profile), C.c_void_p, C.c_int, C.c_int32]


def estimate(n, samples=128, ranges=True, profiles=None, seed=894871):
    rng = C.c_uint64(seed)
    out = Equity()
    n.lib.poker_equity(n.storage, 0, C.byref(rng), samples, ranges, profiles, C.byref(out))
    assert 0 <= out.share <= 1 and 0 <= out.payout <= out.eligible_pot + .01
    assert .99 <= out.effective_samples <= samples + .01
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, default=ROOT / 'build/poker/equity_v1')
    args = p.parse_args()
    n = Native(library=args.build/'libpoker_fullhouse2.so'); configure(n)
    oracles = []
    for hole, board in [(['Ah','Kh'], ['Qh','Jh','Th','2c','3d']),
                        (['2c','3d'], ['As','Ks','Qs','Js','Ts']),
                        (['As','Ad'], ['Kh','7c','2d','9s','4c']),
                        (['As','Kd'], ['Qh','7h','2c','3s','9h'])]:
        replay(n, {'client_pos':1,'hole_cards':hole,'board':board,'action':'b250c/kk/kk/b500'})
        hero = eval7.evaluate([eval7.Card(c) for c in hole+board])
        unseen = [c for c in range(52) if c not in list(map(card, hole+board))]
        for ranges in (False, True):
            num = den = 0.
            for a,b in itertools.combinations(unseen,2):
                rank = eval7.evaluate([eval7.Card(text_card(c)) for c in (a,b)]
                                      + [eval7.Card(c) for c in board])
                # Independent rank oracle; range weights define the distribution
                # being tested, rather than copying the sampler's implementation.
                w = n.lib.poker_range_weight(n.storage,0,0,a,b,None) if ranges else 1
                num += w * (1 if hero > rank else .5 if hero == rank else 0); den += w
            expected = num/den
            got = estimate(n, 50000, ranges)
            assert abs(got.share-expected) < .015, (hole,board,ranges,got.share,expected)
            oracles.append({'hole':hole,'board':board,'ranges':ranges,'exact':expected,
                            'sampled':got.share,'effective_samples':got.effective_samples})
    # Same public history, different hidden cards/runout: equity AND sampled
    # actions/RNG must be identical. Also verify the sampler leaves game state alone.
    rng = random.Random(202610173)
    privacy = legal = 0
    ess = []
    for players in (2,6):
        n=Native(players=players,library=args.build/f'libpoker_fullhouse{players}.so'); configure(n)
        for hand in range(70):
            deck=rng.sample(range(52),2*players+5); stack=rng.choice((700,3000,10000,20000))
            n.reset([stack]*players,deck,hand%players); actions=[]
            while n.snapshot()[0][0]!=255:
                s,rows,_=n.snapshot(); actor=s[0]
                if s[1] and len(actions)%3==0:
                    before=bytes(n.storage); e=estimate(n)
                    assert bytes(n.storage)==before
                    ess.append(e.effective_samples)
                    hidden=[i for i in range(2*players+5) if i not in (2*actor,2*actor+1)
                            and not 2*players<=i<2*players+s[1]+2]
                    changed=deck.copy(); shuffled=[deck[i] for i in hidden];rng.shuffle(shuffled)
                    for i,c in zip(hidden,shuffled): changed[i]=c
                    other=Native(players=players,library=n.path);configure(other)
                    other.reset([stack]*players,changed,hand%players)
                    for a in actions: other.step(a)
                    assert bytes(e)==bytes(estimate(other))
                    variants = [(style,0) for style in (6,7,8) if n.styles & (1 << style)]
                    if hasattr(n.lib, 'poker_decide'):
                        variants += [(7,samples) for samples in (32,64,128)]
                    for style,samples in variants:
                        n.samples=other.samples=samples
                        saved=n.rng.value;other.rng.value=saved
                        assert n.bot(style)==other.bot(style)
                        assert n.rng.value==other.rng.value
                        n.rng.value=saved
                    n.samples=0
                    privacy+=1
                action=n.bot(rng.choice([s for s in (1,5,6,7,8) if n.styles & (1 << s)]))
                if action>0:
                    assert rows[actor][3]+rows[actor][0]>=action>s[3]
                    assert action>=s[3]+s[4] or action==rows[actor][3]+rows[actor][0]
                else: assert action in n.actions()
                n.step(action);actions.append(action);legal+=1
                assert len(actions)<4096
    # Public statistics count each voluntary preflop entry/raise once per hand.
    n=Native(library=args.build/'libpoker_fullhouse2.so'); configure(n)
    profiles=(Profile*2)()
    for _ in range(3):
        n.reset([20000]*2,list(range(9)))
        n.lib.poker_profile_begin(profiles)
        for action in (250,750,0,0,1500,-1):
            n.lib.poker_profile_update(profiles,n.storage,0,action); n.step(action)
        assert n.snapshot()[0][0]==255
    assert [(h.hands,h.vpip,h.pfr) for h in profiles]==[(3,3,3),(3,3,3)]
    assert (profiles[0].faced,profiles[0].folded,profiles[1].post_raises)==(6,3,3)
    # A low-information prior agrees with no history. Strong public evidence of
    # loose/aggressive play changes relative range weights in the intended direction.
    replay(n,{'client_pos':1,'hole_cards':['As','Ad'],'board':['Kh','7c','2d'],
              'action':'b250b750c/b1000'})
    fresh=(Profile*2)(); loose=(Profile*2)(); tight=(Profile*2)()
    loose[0]=Profile(300,240,180,100,10,300,240,200)
    tight[0]=Profile(300,30,15,100,90,300,15,10)
    weights=[]
    for history in (None,fresh,tight,loose):
        weak=n.lib.poker_range_weight(n.storage,0,0,card('4c'),card('5c'),history)
        strong=n.lib.poker_range_weight(n.storage,0,0,card('Ks'),card('Kd'),history)
        weights.append(weak/strong)
    assert weights[0]==weights[1] and weights[2]<weights[0]<weights[3],weights
    estimates=[estimate(n,8192,True,h).share for h in (tight,loose)]
    assert estimates[1]>estimates[0],estimates
    probes=[]
    for hole,board,history in [(['Ah','Kh'],['Qh','7h','2c'],'b250b2000c/b100'),
                               (['As','Ad'],['Kh','7c','2d'],'b250c/b500')]:
        replay(n,{'client_pos':1,'hole_cards':hole,'board':board,'action':history})
        variants=[(s,0) for s in (1,5,6,7,8) if n.styles & (1 << s)]
        if hasattr(n.lib,'poker_decide'):
            variants += [(7,samples) for samples in (32,64,128)]
        for style,samples in variants:
            n.samples=samples
            n.rng.value=29971
            start=time.perf_counter()
            folded=sum(n.bot(style)<0 for _ in range(5000))
            elapsed=time.perf_counter()-start
            probes.append({'hole':hole,'board':board,'history':history,'style':style,'samples':samples,
                           'decisions':5000,'folds':folded,
                           'warm_us_per_decision_with_ctypes':elapsed*1e6/5000})
    print(json.dumps({'river_oracles':oracles,'hidden_card_checks':privacy,'legal_decisions':legal,
        'minimum_ess_128':min(ess),'mean_ess_128':sum(ess)/len(ess),
        'profile_weak_strong_ratios':weights,'equity_vs_tight_loose':estimates,
        'policy_probes':probes},indent=2))


if __name__=='__main__': main()
