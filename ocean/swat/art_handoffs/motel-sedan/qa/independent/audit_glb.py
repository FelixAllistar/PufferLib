#!/usr/bin/env python3
"""Independent glTF 2 binary accessor audit. No Blender/builder functions imported."""
import sys,struct,json,hashlib,io,math
from pathlib import Path
import numpy as np
from PIL import Image
P=Path(sys.argv[1]);O=Path(sys.argv[2]);d=P.read_bytes();magic,version,total=struct.unpack_from('<4sII',d,0)
assert magic==b'glTF' and version==2 and total==len(d)
pos=12;chunks=[]
while pos<len(d):
 n,t=struct.unpack_from('<II',d,pos);pos+=8;chunks.append((t,d[pos:pos+n]));pos+=n
j=json.loads(next(x for t,x in chunks if t==0x4e4f534a));b=next(x for t,x in chunks if t==0x004e4942)
types={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'};nc={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT2':4,'MAT3':9,'MAT4':16}
def acc(i):
 a=j['accessors'][i];v=j['bufferViews'][a['bufferView']];dt=np.dtype(types[a['componentType']]);n=nc[a['type']];off=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',dt.itemsize*n)
 assert not a.get('sparse'), 'Sparse accessor unsupported'
 x=np.ndarray((a['count'],n),dtype=dt,buffer=b,offset=off,strides=(stride,dt.itemsize)).copy()
 if a.get('normalized'):
  lim=np.iinfo(dt);x=np.maximum(x/lim.max,-1)
 return x
access=[]
for i,a in enumerate(j.get('accessors',[])):
 x=acc(i);access.append({'index':i,'count':len(x),'type':a['type'],'componentType':a['componentType'],'finite':bool(np.isfinite(x).all()),'min':x.min(axis=0).tolist(),'max':x.max(axis=0).tolist()})
mesh=[];bounds=[];totaltri=0;verts=0;allpr=[]
for mi,m in enumerate(j['meshes']):
 prs=[]
 for pi,p in enumerate(m['primitives']):
  assert p.get('mode',4)==4
  at=p['attributes'];v=acc(at['POSITION']);idx=acc(p['indices']).reshape(-1).astype(int) if 'indices'in p else np.arange(len(v));assert len(idx)%3==0;f=idx.reshape(-1,3);assert f.min()>=0 and f.max()<len(v)
  q=v[f].astype(float);cr=np.cross(q[:,1]-q[:,0],q[:,2]-q[:,0]);twicearea=np.linalg.norm(cr,axis=1);zero=twicearea<1e-12;fn=cr/np.maximum(twicearea,1e-30)[:,None];n=acc(at['NORMAL']);nl=np.linalg.norm(n,axis=1);dot=(n[f]*fn[:,None,:]).sum(axis=2)
  uv=acc(at['TEXCOORD_0']);uvq=uv[f].astype(float);uvd=uvq[:,1]-uvq[:,0];uve=uvq[:,2]-uvq[:,0];uva=np.abs(uvd[:,0]*uve[:,1]-uvd[:,1]*uve[:,0]);uvzero=uva<1e-12
  pr={'primitive':pi,'material':j['materials'][p.get('material',0)]['name'],'vertices':len(v),'triangles':len(f),'zero_area_triangles':int(zero.sum()),'zero_area_triangle_ids':np.where(zero)[0].tolist(),'uv_zero_area_triangles':int(uvzero.sum()),'uv_zero_area_triangle_ids':np.where(uvzero)[0].tolist(),'normal_length_min':float(nl.min()),'normal_length_max':float(nl.max()),'corner_normal_opposes_triangle':int((dot<-.05).sum()),'triangles_with_opposing_corner_normal':int((dot<-.05).any(axis=1).sum()),'triangle_average_normal_opposes_face':int((dot.mean(axis=1)<-.05).sum()),'minimum_normal_face_dot':float(dot.min()),'uv_min':uv.min(axis=0).tolist(),'uv_max':uv.max(axis=0).tolist(),'bbox_min':v.min(axis=0).tolist(),'bbox_max':v.max(axis=0).tolist(),'all_finite':bool(np.isfinite(v).all() and np.isfinite(n).all() and np.isfinite(uv).all())}
  prs.append(pr);allpr.append(pr);bounds.append(v);totaltri+=len(f);verts+=len(v)
 mesh.append({'mesh':mi,'name':m.get('name'),'primitives':prs})
ims=[]
for i,im in enumerate(j.get('images',[])):
 v=j['bufferViews'][im['bufferView']];data=b[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']];p=Image.open(io.BytesIO(data));p.load();ims.append({'index':i,'name':im.get('name'),'mimeType':im.get('mimeType'),'embedded':True,'size':p.size,'mode':p.mode,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'pixel_sha256':hashlib.sha256(np.asarray(p).tobytes()).hexdigest()})
materials=[]
for m in j['materials']:
 materials.append({'name':m.get('name'),'alphaMode':m.get('alphaMode','OPAQUE'),'doubleSided':m.get('doubleSided',False),'extensions':m.get('extensions',{}),'pbr':m.get('pbrMetallicRoughness',{})})
nodes=[]
for i,n in enumerate(j['nodes']):
 transform={k:n[k] for k in ['translation','rotation','scale','matrix'] if k in n};finite=all(np.isfinite(v).all() for v in transform.values());nodes.append({'index':i,'name':n.get('name'),'mesh':n.get('mesh'),'children':n.get('children',[]),'transform':transform,'finite':bool(finite),'extras':n.get('extras')})
bound=np.concatenate(bounds)
r={'path':str(P),'sha256':hashlib.sha256(d).hexdigest(),'bytes':len(d),'header':{'version':version,'declared_length':total},'glTF_asset':j['asset'],'totals':{'meshes':len(mesh),'nodes':len(nodes),'materials':len(materials),'images':len(ims),'primitives':len(allpr),'triangles':totaltri,'export_vertices':verts,'zero_area_triangles':sum(p['zero_area_triangles'] for p in allpr),'zero_uv_area_triangles':sum(p['uv_zero_area_triangles'] for p in allpr),'opposing_corner_normals':sum(p['corner_normal_opposes_triangle'] for p in allpr),'average_opposing_triangles':sum(p['triangle_average_normal_opposes_face'] for p in allpr)},'bbox_mesh_coordinate_min':bound.min(axis=0).tolist(),'bbox_mesh_coordinate_max':bound.max(axis=0).tolist(),'dimensions':(bound.max(axis=0)-bound.min(axis=0)).tolist(),'scenes':j.get('scenes'),'nodes':nodes,'materials':materials,'images':ims,'meshes':mesh,'accessors':access,'animations':len(j.get('animations',[])),'skins':len(j.get('skins',[])),'cameras':len(j.get('cameras',[])),'extensions_used':j.get('extensionsUsed',[]),'external_uris':[x['uri'] for k in ['images','buffers'] for x in j.get(k,[]) if 'uri'in x]}
O.write_text(json.dumps(r,indent=2));print(json.dumps({k:r[k]for k in ['sha256','totals','dimensions','external_uris']},indent=2))
