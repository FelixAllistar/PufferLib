"""Exhaustive independent eval7 check for the exact-river experiment."""
import ctypes as C
import itertools
import json
from pathlib import Path
import sys
import eval7
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'ocean/poker'))
from eval import Native, replay, card, text_card
from test_equity import configure, Equity

n=Native(library='build/poker/strength_v1/river/libpoker_fullhouse2.so');configure(n)
cases=[(['Ah','Kh'],['Qh','Jh','Th','2c','3d']),
       (['2c','3d'],['As','Ks','Qs','Js','Ts']),
       (['As','Ad'],['Kh','7c','2d','9s','4c']),
       (['As','Kd'],['Qh','7h','2c','3s','9h']),
       (['Ad','Td'],['Ah','Js','Jh','3d','Qh']),
       (['Th','Tc'],['Qd','Qc','8c','3c','7h']),
       (['Kd','Td'],['Kh','6d','3h','3d','3s']),
       (['6s','5s'],['4c','7h','8c','Ah','Ad'])]
results=[]
for hole,board in cases:
 replay(n,{'client_pos':1,'hole_cards':hole,'board':board,'action':'b250c/kk/kk/b500'})
 hero=eval7.evaluate([eval7.Card(c) for c in hole+board]);known=set(map(card,hole+board))
 unseen=[c for c in range(52) if c not in known];num=den=0.
 for a,b in itertools.combinations(unseen,2):
  other=eval7.evaluate([eval7.Card(text_card(c)) for c in (a,b)]+[eval7.Card(c) for c in board])
  w=n.lib.poker_range_weight(n.storage,0,0,a,b,None)
  num+=w*(1 if hero>other else .5 if hero==other else 0);den+=w
 expected=num/den;before=bytes(n.storage);outcomes=[]
 for seed in (7,1820937):
  rng=C.c_uint64(seed);e=Equity()
  n.lib.poker_equity(n.storage,0,C.byref(rng),128,2,None,C.byref(e))
  assert abs(e.share-expected)<1e-7
  assert abs(e.payout-1500*expected)<1e-4
  assert bytes(n.storage)==before and rng.value==seed
  outcomes.append(bytes(e))
 assert outcomes[0]==outcomes[1]
 results.append({'hole':hole,'board':board,'expected_share':expected,'exact_native_share':e.share,'effective_combinations':e.effective_samples})
result={'combinations_per_position':990,'positions':results,'seed_independence':True,'game_state_unchanged':True}
Path('reports/poker/20261018/strength_v1/river_oracle.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: eight exact river oracles, seed independence and state isolation')
