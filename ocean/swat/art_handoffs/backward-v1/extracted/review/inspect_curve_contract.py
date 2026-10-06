"""Independent source-window identity and complete cubic quaternion norm extrema."""
import argparse, json, hashlib
from pathlib import Path
from collections import Counter
import numpy as np
from replay_unit import GLB
ap=argparse.ArgumentParser(description='Replay saved boundary-key identity and every cubic quaternion norm interval with Python and NumPy.')
ap.add_argument('--package',type=Path,required=True)
ap.add_argument('--output',type=Path,required=True)
ap.add_argument('--source-blend',type=Path,help='Optional separately obtained pinned native Blender file for direct byte-identity verification.')
args=ap.parse_args();package=args.package
g=GLB(package/'walk_backward_shared_ready_n_c1_loop_a.glb')
s=json.loads((package/'SOURCE_CURVES.json').read_text())
d=json.loads((package/'SOURCE_SAVED_CURVE_DIFF.json').read_text())
provenance=json.loads((package/'SOURCE_PROVENANCE.json').read_text())
assert s['source_sha256']==d['candidate_sha256']==next(r['sha256'] for r in provenance['inputs'] if r['role']=='editable')
source_bytes_sha=hashlib.sha256(args.source_blend.read_bytes()).hexdigest() if args.source_blend else None
if source_bytes_sha is not None:assert source_bytes_sha==s['source_sha256']
lookup={(c['data_path'],c['component']):c for c in s['curves']}
checked=0
for c in d['all_curves']:
 target=lookup[(c['path'],c['component'])];assert len(target['keys'])==c['candidate_key_count']
 keys={k[0]:k for k in target['keys']}
 for k in c['candidate_window_keys']:
  frame=k['co'][0];expected=k['co']+k['hl']+k['hr']+[k['interp']]
  assert keys[frame]==expected,(c['path'],c['component'],frame)
  checked+=1
counts=Counter(k[6] for c in s['curves'] for k in c['keys']);assert len(lookup)==700 and sum(counts.values())==168700
minimum=(float('inf'),None);maximum=(0,None);intervals=0;critical=0
for (node,path),(ti,v) in g.channels.items():
 if path!='rotation':continue
 for i,h in enumerate(np.diff(ti)):
  p=v[i,1];q=v[i+1,1];a=h*v[i,2];b=h*v[i+1,0]
  co=np.array([p,a,-3*p-2*a+3*q-b,2*p+a-2*q+b])
  sq=np.zeros(7)
  for j in range(4):
   product=np.polynomial.polynomial.polymul(co[:,j],co[:,j])
   sq[:len(product)]+=product
  probe=.3819660112501051
  direct=(2*probe**3-3*probe**2+1)*p+(probe**3-2*probe**2+probe)*a+(-2*probe**3+3*probe**2)*q+(probe**3-probe**2)*b
  assert abs(np.polynomial.polynomial.polyval(probe,sq)-float(direct@direct))<1e-12
  derivative=np.polynomial.polynomial.polyder(sq)
  candidates=[0.,1.]
  if np.any(derivative):
   roots=np.polynomial.polynomial.polyroots(derivative)
   candidates.extend(float(z.real) for z in roots if abs(z.imag)<1e-9 and 0<z.real<1)
  critical+=len(candidates)-2;intervals+=1
  for u in candidates:
   norm=float(np.linalg.norm(np.polynomial.polynomial.polyval(u,co)))
   item={'node':node,'name':g.names[node],'segment':i,'time_s':float(ti[i]+u*h),'segment_u':u}
   if norm<minimum[0]:minimum=(norm,item)
   if norm>maximum[0]:maximum=(norm,item)
assert minimum[0]>.99
r={'status':'PASS','glb_sha256':g.sha,'source_sha256':s['source_sha256'],'optional_direct_source_bytes_sha256':source_bytes_sha,'source_curves':{'count':len(lookup),'keys':sum(counts.values()),'interpolation_counts':dict(counts),'saved_window_keys_exact':checked,'all_candidate_key_counts_match':True,'boundary_changed_curves':sum(c['changed'] for c in d['all_curves'])},'quaternion_polynomial_norm':{'method':'Each actual stored float32 cubic quaternion is expanded as a cubic in unit segment u. Its squared norm is degree6; all numerically real roots of its derivative inside[0,1] and both endpoints are evaluated. Numerical root-based extrema, not exact symbolic certification.','intervals':intervals,'squared_norm_coefficients_cross_checked_all_intervals':True,'interior_critical_points':critical,'minimum':minimum[0],'minimum_location':minimum[1],'maximum':maximum[0],'maximum_location':maximum[1]}}
args.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
