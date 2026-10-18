import sys,runpy,contextlib,io,json,pathlib,hashlib,numpy as np
SCRIPT=pathlib.Path(__file__).with_name('audit_glb.py')
def canonical(path,out):
 sys.argv=[str(SCRIPT),path,str(out)]
 with contextlib.redirect_stdout(io.StringIO()):d=runpy.run_path(str(SCRIPT))
 j=d['j'];ac=d['accessor'];triangles=[]
 for ni,M in d['worlds'].items():
  node=j['nodes'][ni]
  if 'mesh' not in node:continue
  for p in j['meshes'][node['mesh']]['primitives']:
   a=p['attributes'];pos=ac(a['POSITION']);pos=(np.c_[pos,np.ones(len(pos))]@M.T)[:,:3];norm=ac(a['NORMAL'])@np.linalg.inv(M[:3,:3]);uv=ac(a['TEXCOORD_0']);verts=np.c_[pos,norm,uv];idx=ac(p['indices']).reshape(-1,3).astype(int);mat=j['materials'][p['material']]['name']
   for tri in idx:
    corners=[tuple(map(float,verts[k])) for k in tri]
    oriented=min(tuple(corners[k:]+corners[:k]) for k in range(3));triangles.append((mat,oriented))
 triangles.sort();b=repr(triangles).encode();return triangles,hashlib.sha256(b).hexdigest()
paths=sys.argv[1:];out=pathlib.Path(__file__).parent
A,ha=canonical(paths[0],out/'compare_a_audit.json');B,hb=canonical(paths[1],out/'compare_b_audit.json');r={'files':paths,'triangle_count_a':len(A),'triangle_count_b':len(B),'exact_world_triangle_material_normal_uv_match':A==B,'canonical_oriented_surface_sha256_a':ha,'canonical_oriented_surface_sha256_b':hb,'scope':'Exact float values, material names and oriented triangle corners; independent of mesh grouping, triangle order, and cyclic vertex-start order. Includes POSITION, NORMAL, TEXCOORD_0.'};(out/'packed_surface_comparison.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
