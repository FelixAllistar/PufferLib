"""Check a recorded policy's exact actions/RNG against another native binary."""
import argparse
import json
from pathlib import Path
from eval import Native, replay

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
p.add_argument('--library',required=True)
p.add_argument('--style',type=int,required=True)
p.add_argument('--equity-samples',type=int,choices=(0,32,64,128),default=0)
p.add_argument('--output',type=Path,required=True)
args=p.parse_args()
n=Native(library=args.library,samples=args.equity_samples)
checked=0; changed=[]
for line in args.log.read_text().splitlines():
    hand=json.loads(line)
    if 'winnings_chips' not in hand: continue
    for d in hand.get('decisions',[]):
        replay(n,d); n.rng.value=d['policy_rng_before']
        action=n.bot(args.style); checked+=1
        if action!=d['native_action'] or n.rng.value!=d['policy_rng_after']:
            changed.append({'hand':hand['hand'],'history':d['action'],
                            'recorded_action':d['native_action'],'replayed_action':action,
                            'rng_matches':n.rng.value==d['policy_rng_after']})
result={'log':str(args.log),'library':args.library,'style':args.style,'equity_samples':args.equity_samples,
        'decisions_checked':checked,'changed':changed}
args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'decisions_checked':checked,'changed':len(changed)}))
