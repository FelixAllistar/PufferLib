"""Ground anchor proof directly from actual exported GLB position buffers."""
import json,runpy
from pathlib import Path
R=Path(__file__).resolve().parents[1]
d=runpy.run_path(str(R/'source/audit_glb.py'));points=d['ps'];contacts=[p for p in points if abs(p[1])<1e-7]
assert len(contacts)>0 and min(p[1]for p in points)>=-1e-7
anchors=[]
for name,x,z in [('front_left',-.410,.230),('front_right',.410,.230),('rear_left',-.410,-.230),('rear_right',.410,-.230)]:
 q=list(set(p for p in contacts if abs(p[0]-x)<.03 and abs(p[2]-z)<.04));assert len(q)>=2
 lo=[min(p[i]for p in q)for i in range(3)];hi=[max(p[i]for p in q)for i in range(3)];center=[(a+b)/2 for a,b in zip(lo,hi)]
 assert max(abs(center[i]-[x,0,z][i])for i in range(3))<1e-6
 anchors.append({'name':name,'measured_center_xyz_m':center,'wheel_contact_bounds_min_m':lo,'wheel_contact_bounds_max_m':hi,'unique_contact_vertices':len(q)})
report={'runtime_sha256':d['r']['sha256'],'coordinate_system':'GLB: metres, Y up, +Z front','root_anchor_m':[0,0,0],'ground_plane_y_m':0,'below_ground_vertices':0,'four_measured_wheel_contacts':anchors}
(R/'qa/ground_anchor_audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
