"""Full official-environment games, with optional stepwise native-core parity."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import numpy as np
from kaggle_environments import make
from kaggle_environments.agent import get_last_callable

def main():
    p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('--steps',type=int,default=720)
    p.add_argument('--runner',action='store_true')
    a=p.parse_args()
    for seed in (7,42):
        for seat in (0,1):
            source=a.package.resolve()/'main.py'
            # Same compile/exec entry point used by Kaggle: no __file__ supplied.
            play=get_last_callable(source.read_text(),path=str(source))
            e=make('kaggriculture',configuration={'seed':seed,'episodeSteps':720},debug=True);e.reset()
            if a.runner:
                def idle(obs):
                    return {'farmer':['PASS'],'hands':[['PASS'] for _ in obs.farms[obs.player].hands],'market':[]}
                agents=[idle,idle];agents[seat]=str(source);e.run(agents)
                print('RUNNER',seed,seat,'money',e.state[seat].observation.farms[seat].money,
                      'statuses',[s.status for s in e.state],flush=True)
                assert all(s.status=='DONE' for s in e.state)
                continue
            times=[]
            for step in range(a.steps-1):
                actions=[]
                for pid in (0,1):
                    obs=json.loads(json.dumps(e.state[pid].observation))
                    if pid==seat:
                        t=time.perf_counter();action=play(obs,e.configuration);times.append(time.perf_counter()-t)
                    else:action={'farmer':['PASS'],'hands':[['PASS'] for _ in obs['farms'][pid]['hands']],'market':[]}
                    actions.append(action)
                e.step(actions)
                assert all(s.status not in ('ERROR','INVALID','TIMEOUT') for s in e.state),e.state
            print('GAME',seed,seat,'money',e.state[seat].observation.farms[seat].money,
                  'statuses',[s.status for s in e.state],'mean_ms',1000*np.mean(times),'max_ms',1000*max(times),flush=True)
            if a.steps==720:assert all(s.status=='DONE' for s in e.state)
            play.__globals__['_CONTROLLER'].close()

if __name__=='__main__':main()
