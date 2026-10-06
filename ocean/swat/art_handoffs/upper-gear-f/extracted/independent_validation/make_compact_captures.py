"""Losslessly subset direct evaluated references; dense originals remain unchanged."""
import numpy as np,json,hashlib
from pathlib import Path
O=Path(__file__).resolve().parent
for name in ['source_capture','reimport_capture']:
 pin=hashlib.sha256((O/(name+'.npz')).read_bytes()).hexdigest();meta=json.loads((O/(name+'.json')).read_text());assert meta['time_axis_last'];z=dict(np.load(O/(name+'.npz')));indices=np.arange(0,361,30);selected={}
 for k,v in z.items():selected[k]=v[...,indices] if k.startswith('motion_eval') else v
 out=O/(name+'_compact.npz');np.savez_compressed(out,**selected)
 with np.load(out) as check:assert all(np.array_equal(check[k],v) for k,v in selected.items())
 meta['schema']='independent-upper-gear-f-compact-capture/1';meta['dense_reference_npz_sha256']=pin;meta['direct_evaluated_control_indices']={'neutral':[0],'motion':indices.tolist()};meta['direct_evaluated_control_times_s']={'neutral':[0.],'motion':z['motion_times'][indices].tolist()};meta['capture_npz_sha256']=hashlib.sha256(out.read_bytes()).hexdigest();meta['scope']='Complete bind positions, weights, topology, material IDs and UVs; every joint matrix at all361 motion times. Direct evaluated mesh vertices retained only at13 quarter-second controls. Full dense direct-reference audit is separately reported.'
 (O/(name+'_compact.json')).write_text(json.dumps(meta,indent=2)+'\n');assert hashlib.sha256((O/(name+'.npz')).read_bytes()).hexdigest()==pin;print(name,out.stat().st_size,flush=True)
