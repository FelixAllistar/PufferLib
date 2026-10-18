import json,struct,sys,hashlib,pathlib,collections
import numpy as np
p=pathlib.Path(sys.argv[1]); raw=p.read_bytes(); magic,version,length=struct.unpack_from('<4sII',raw); assert magic==b'glTF' and version==2 and length==len(raw)
chunks=[]; off=12
while off<len(raw):
 n,t=struct.unpack_from('<II',raw,off); off+=8; chunks.append((t,raw[off:off+n])); off+=n
j=json.loads(next(b for t,b in chunks if t==0x4e4f534a)); binary=next(b for t,b in chunks if t==0x004e4942)
dtypes={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}; dims={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def accessor(i):
 a=j['accessors'][i]; v=j['bufferViews'][a['bufferView']]; dt=np.dtype(dtypes[a['componentType']]); n=dims[a['type']]; off=v.get('byteOffset',0)+a.get('byteOffset',0)
 return np.ndarray((a['count'],n),dtype=dt,buffer=binary,offset=off,strides=(v.get('byteStride',dt.itemsize*n),dt.itemsize)).copy()
def localmat(n):
 if 'matrix' in n:return np.array(n['matrix']).reshape((4,4),order='F')
 x,y,z,w=n.get('rotation',[0,0,0,1]); R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
 T=np.eye(4); T[:3,:3]=R@np.diag(n.get('scale',[1,1,1])); T[:3,3]=n.get('translation',[0,0,0]); return T
worlds={}
def visit(i,parent):
 worlds[i]=parent@localmat(j['nodes'][i])
 for ch in j['nodes'][i].get('children',[]):visit(ch,worlds[i])
for i in j['scenes'][j.get('scene',0)]['nodes']:visit(i,np.eye(4))
report={'file':str(p),'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'asset':j.get('asset'),'extensionsUsed':j.get('extensionsUsed',[]),'materials':j.get('materials',[]),'images':[],'nodes':[],'mesh_instances':[],'warnings':[]}
allpositions=[]; totaltris=0; totalvertices=0
for i,img in enumerate(j.get('images',[])):
 if 'bufferView' in img:
  v=j['bufferViews'][img['bufferView']]; b=binary[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']]
  h=hashlib.sha256(b).hexdigest(); matches=[]
  for root in p.parents:
   if root.name=='candidate':
    matches=[str(f.relative_to(root)) for f in root.rglob('*') if f.is_file() and f.suffix.lower() in ('.png','.jpg','.jpeg') and hashlib.sha256(f.read_bytes()).hexdigest()==h]; break
  report['images'].append({'index':i,'name':img.get('name'),'mimeType':img.get('mimeType'),'bytes':len(b),'sha256':h,'exact_source_matches':matches})
 else:report['images'].append({'index':i,**img})
for ni,M in worlds.items():
 node=j['nodes'][ni]; report['nodes'].append({'index':ni,'name':node.get('name'),'mesh':node.get('mesh'),'world_matrix':M.tolist(),'children':node.get('children',[])})
 if 'mesh' not in node:continue
 mesh=j['meshes'][node['mesh']]; positions=[]; faces=[]; offset=0; primitives=[]
 for pr in mesh['primitives']:
  a=pr['attributes']; pos=accessor(a['POSITION']); v=(np.c_[pos,np.ones(len(pos))]@M.T)[:,:3]; positions.append(v); idx=accessor(pr['indices']).reshape(-1,3).astype(int) if 'indices' in pr else np.arange(len(pos)).reshape(-1,3)
  assert pr.get('mode',4)==4; faces.append(idx+offset); offset+=len(pos)
  cross=np.cross(v[idx[:,1]]-v[idx[:,0]],v[idx[:,2]]-v[idx[:,0]]); ar=np.linalg.norm(cross,axis=1)/2; normalstats={}
  if 'NORMAL' in a:
   n=accessor(a['NORMAL'])@np.linalg.inv(M[:3,:3]); norm=np.linalg.norm(n,axis=1); unit=n/np.maximum(norm[:,None],1e-30); dots=np.sum(cross[:,None,:]*unit[idx],axis=2)/np.maximum(2*ar[:,None],1e-30)
   normalstats={'finite':bool(np.isfinite(n).all()),'length_min':float(norm.min()),'length_max':float(norm.max()),'winding_opposed_corner_count':int((dots < -1e-5).sum()),'face_corner_dot_min':float(dots.min())}
  uvs={}
  for an,ai in a.items():
   if an.startswith('TEXCOORD'):
    uv=accessor(ai); uvs[an]={'finite':bool(np.isfinite(uv).all()),'min':uv.min(axis=0).tolist(),'max':uv.max(axis=0).tolist(),'outside_0_1_vertices':int(np.any((uv<0)|(uv>1),axis=1).sum()),'zero_area_triangles':int((np.abs(np.cross(uv[idx[:,1]]-uv[idx[:,0]],uv[idx[:,2]]-uv[idx[:,0]]))<1e-14).sum())}
  primitives.append({'material':pr.get('material'),'attributes':list(a),'vertices':len(v),'triangles':len(idx),'area_min_m2':float(ar.min()),'degenerate_triangles_below_1e-12_m2':int((ar<1e-12).sum()),'normals':normalstats,'uvs':uvs})
 v=np.concatenate(positions); f=np.concatenate(faces); allpositions.append(v); totaltris+=len(f); totalvertices+=len(v)
 # Spatial welding removes glTF corner duplication due to UV/normal seams.
 q=np.round(v/1e-6).astype(np.int64); uniq,first,inv=np.unique(q,axis=0,return_index=True,return_inverse=True); wv=v[first]; wf=inv[f]
 ed=collections.defaultdict(list); vertfaces=collections.defaultdict(list)
 for fi,t in enumerate(wf):
  for vert in t:vertfaces[int(vert)].append(fi)
  for a,b in zip(t,np.roll(t,-1)):ed[tuple(sorted((int(a),int(b))))].append((fi,int(a),int(b)))
 seen=set(); components=[]
 for seed in range(len(wf)):
  if seed in seen:continue
  pending=[seed]; seen.add(seed); ids=[]
  while pending:
   fi=pending.pop();ids.append(fi)
   for vert in wf[fi]:
    for nxt in vertfaces[int(vert)]:
     if nxt not in seen:seen.add(nxt);pending.append(nxt)
  ids=np.array(ids); c=wf[ids]; verts=np.unique(c); ids_set=set(ids.tolist()); es={k:x for k,x in ed.items() if x[0][0] in ids_set}; volume=np.einsum('ij,ij->i',wv[c[:,0]],np.cross(wv[c[:,1]],wv[c[:,2]])).sum()/6
  components.append({'triangles':len(c),'welded_vertices':len(verts),'edges':len(es),'boundary_edges':sum(len(x)==1 for x in es.values()),'nonmanifold_edges_over_2':sum(len(x)>2 for x in es.values()),'same_direction_paired_edges':sum(len(x)==2 and x[0][1:]==x[1][1:] for x in es.values()),'euler_characteristic':len(verts)-len(es)+len(c),'signed_volume_m3':float(volume),'world_min':wv[verts].min(axis=0).tolist(),'world_max':wv[verts].max(axis=0).tolist()})
 report['mesh_instances'].append({'node':ni,'name':node.get('name'),'mesh_name':mesh.get('name'),'primitives':primitives,'world_min':v.min(axis=0).tolist(),'world_max':v.max(axis=0).tolist(),'weld_tolerance_m':1e-6,'welded_vertices':len(wv),'components':components,'duplicate_triangle_count':len(wf)-len(np.unique(np.sort(wf,axis=1),axis=0))})
v=np.concatenate(allpositions); report['summary']={'node_count':len(j.get('nodes',[])),'mesh_instances':len(report['mesh_instances']),'mesh_count':len(j.get('meshes',[])),'material_count':len(j.get('materials',[])),'image_count':len(j.get('images',[])),'triangles':totaltris,'exported_vertices':totalvertices,'world_min_gltf_xyz':v.min(axis=0).tolist(),'world_max_gltf_xyz':v.max(axis=0).tolist(),'dimensions_gltf_xyz_m':np.ptp(v,axis=0).tolist(),'floor_y_m':float(v[:,1].min()),'all_positions_finite':bool(np.isfinite(v).all()),'solid_components':sum(len(m['components']) for m in report['mesh_instances'])}
out=p.with_suffix('.audit.json') if len(sys.argv)<3 else pathlib.Path(sys.argv[2]);out.write_text(json.dumps(report,indent=2));print(json.dumps(report['summary'],indent=2));print(out)
