import runpy,json,math
from pathlib import Path
R=Path(__file__).resolve().parents[1];g=runpy.run_path(str(R/'source/audit_glb.py'));j=g['j'];acc=g['acc'];pr=j['meshes'][0]['primitives'][0];ps=acc(pr['attributes']['POSITION']);ix=[x[0]for x in acc(pr['indices'])]
def sub(a,b):return [a[i]-b[i]for i in range(3)]
def dot(a,b):return sum(a[i]*b[i]for i in range(3))
def point_on_triangle(p,a,b,c):
 u=sub(b,a);v=sub(c,a);w=sub(p,a);uu=dot(u,u);vv=dot(v,v);uv=dot(u,v);wu=dot(w,u);wv=dot(w,v);den=uu*vv-uv*uv
 if abs(den)<1e-20:return False
 s=(vv*wu-uv*wv)/den;t=(uu*wv-uv*wu)/den
 return s>=-1e-6 and t>=-1e-6 and s+t<=1+1e-6 and sum((a[i]+s*u[i]+t*v[i]-p[i])**2 for i in range(3))<1e-14
bottom=[p for p in ps if abs(p[1])<1e-7];base=[max(bottom,key=lambda p:sign*p[axis])for axis,sign in [(0,1),(0,-1),(2,1),(2,-1)]]
anchors=[]
for y in [.093,.345]:
 p=[0,y,-.134];matches=[k//3 for k in range(0,len(ix),3) if point_on_triangle(p,*[ps[ix[k+i]]for i in range(3)])];assert matches;anchors.append({'name':'wall_mount_lower' if y<.2 else 'wall_mount_upper','position_gltf_m':p,'actual_triangle_indices':matches,'normal_gltf':[0,0,-1]})
r={'coordinate_system':'GLTF metres Y up, front +Z','origin':'Bottom center of pressure cylinder, Y=0','wall_plane_z_m':-.134,'base_contact_vertices':base,'wall_contacts':anchors,'wall_clearance_min_z_m':min(p[2]for p in ps),'all_geometry_in_front_of_wall':min(p[2]for p in ps)>=-.1340001,'measurement':'Contacts tested against actual exported mesh triangles; base samples are actual exported vertices. These are asset-local art anchors, not installed collision.'};assert r['all_geometry_in_front_of_wall'];(R/'qa/contact_anchors.json').write_text(json.dumps(r,indent=2))
