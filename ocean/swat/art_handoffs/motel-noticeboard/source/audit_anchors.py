"""Measure mounting references against actual raw GLB rear triangles."""
import runpy,json
from pathlib import Path
R=Path(__file__).resolve().parents[1];g=runpy.run_path(str(R/'source/audit_glb.py'));j=g['j'];acc=g['acc'];anchors=json.loads((R/'qa/mounting.json').read_text())['mount_anchor_centers_m'];out=[]
for x,y,z in anchors:
 hits=[]
 for mesh in j['meshes']:
  for pr in mesh['primitives']:
   p=acc(pr['attributes']['POSITION']);idx=[v[0]for v in acc(pr['indices'])]
   for k in range(0,len(idx),3):
    a,b,c=[p[i] for i in idx[k:k+3]]
    if max(abs(v[2]-z) for v in [a,b,c])>1e-7:continue
    den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if abs(den)<1e-12:continue
    u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den;v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den
    if min(u,v,1-u-v)>-1e-7:hits.append(k//3)
 assert hits;out.append({'reference_m':[x,y,z],'on_actual_rear_frame_surface':True,'hit_triangles':hits})
(R/'qa/anchor_audit.json').write_text(json.dumps({'method':'Barycentric containment on actual GLB rear triangles at Z=0','reference_spacing_m':anchors[1][0]-anchors[0][0],'references':out},indent=2))
