"""Inventory published NPZ artifacts in locally fetched Fullhouse refs.

Fetch tags in the isolated external checkout first; this never changes its HEAD
or executes external code. Requires NumPy solely to inspect safe array archives.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import subprocess

import numpy as np


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('build/poker/external/fullhouse-bot'))
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    def git(*commands):
        return subprocess.check_output(['git','-C',str(args.source),*commands])
    refs=['HEAD']+git('tag').decode().splitlines()
    rows=[];blobs=set();other_checkpoints=[]
    for ref in refs:
        artifacts=[]
        for line in git('ls-tree','-r',ref).decode().splitlines():
            _mode,_kind,blob,path=line.split(None,3)
            if path.endswith('.npz'):
                artifacts.append({'path':path,'blob':blob});blobs.add(blob)
            elif path.endswith(('.pt','.pth')):
                other_checkpoints.append({'ref':ref,'path':path,'blob':blob})
        rows.append({'ref':ref,'commit':git('rev-parse',ref).decode().strip(),'artifacts':artifacts})
    models=[]
    for blob in sorted(blobs):
        raw=git('cat-file','blob',blob)
        with np.load(io.BytesIO(raw),allow_pickle=False) as a:
            kind='average_strategy' if 'strategy_w0' in a.files else 'advantage' if 'trunk_w0' in a.files else 'table'
            models.append({'blob':blob,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),
                           'format':kind,'shapes':{k:list(a[k].shape) for k in a.files}})
    data={'observed_utc':datetime.now(timezone.utc).isoformat(),
          'source':'https://github.com/advitrocks9/fullhouse-bot','refs':rows,'unique_arrays':models,
          'other_checkpoints':other_checkpoints,
          'interpretation':'Tracked artifacts in these refs only; not every historical commit or private checkpoint. The shipped model is an advantage network. Training code supporting average-strategy export does not make the supplied checkpoint an implementation bug.'}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({'refs':len(rows),'unique_arrays':len(models),
        'average_strategy_artifacts':sum(m['format']=='average_strategy' for m in models),
        'advantage_artifacts':sum(m['format']=='advantage' for m in models),
        'other_checkpoints':len(other_checkpoints)},indent=2))


if __name__=='__main__': main()
