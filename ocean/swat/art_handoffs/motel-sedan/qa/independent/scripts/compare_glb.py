#!/usr/bin/env python3
"""Semantic comparison independent of export ordering; triangles retain cyclic winding."""
import sys,json,struct,hashlib
from pathlib import Path
import numpy as np
DT={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'};NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def read(path):
 data=Path(path).read_bytes();pos=12
 while pos<len(data):
  n,t=struct.unpack_from('<II',data,pos);pos+=8;c=data[pos:pos+n];pos+=n
  if t==0x4e4f534a:j=json.loads(c)
  else:b=c
 def a(i):
  ac=j['accessors'][i];v=j['bufferViews'][ac['bufferView']];dt=np.dtype(DT[ac['componentType']]);nc=NC[ac['type']];return np.ndarray((ac['count'],nc),dtype=dt,buffer=b,offset=v.get('byteOffset',0)+ac.get('byteOffset',0),strides=(v.get('byteStride',nc*dt.itemsize),dt.itemsize)).copy()
 groups={}
 for mesh in j['meshes']:
  faces=[]
  for p in mesh['primitives']:
   at=p['attributes'];v=a(at['POSITION']);n=a(at['NORMAL']);uv=a(at['TEXCOORD_0']);fv=np.concatenate((v,n,uv),axis=1);q=np.rint(fv*1e6).astype('int64');ind=a(p['indices']).reshape(-1,3);mat=j['materials'][p['material']]['name']
   for face in ind:
    cs=tuple(tuple(q[i].tolist())for i in face);canonical=min(cs,cs[1:]+cs[:1],cs[2:]+cs[:2]);faces.append((mat,canonical))
  faces.sort();groups[mesh['name']]={'triangles':len(faces),'canonical_triangle_sha256_1e6':hashlib.sha256(repr(faces).encode()).hexdigest()}
 images={}
 for im in j['images']:
  v=j['bufferViews'][im['bufferView']];imdata=b[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']];images[im.get('name')]=hashlib.sha256(imdata).hexdigest()
 # Resolve texture references to image names and bytes for fair order-insensitive material comparison.
 def norm(o):
  if isinstance(o,list):return [norm(x)for x in o]
  if isinstance(o,dict):
   d={k:norm(v)for k,v in o.items()}
   if 'index'in o and 'texCoord'in o or ('index'in o and len(o)==1):
    tx=j['textures'][o['index']];im=j['images'][tx['source']];d['index']={'image':im.get('name'),'image_sha256':images[im.get('name')]}
   return d
  return o
 materials={m['name']:norm(m)for m in j['materials']}
 return {'path':str(path),'sha256':hashlib.sha256(data).hexdigest(),'groups':groups,'images':images,'materials':materials}
a=read(sys.argv[1]);b=read(sys.argv[2]);r={'a':a,'b':b,'byte_identical':a['sha256']==b['sha256'],'geometry_match_1e6':a['groups']==b['groups'],'images_match':a['images']==b['images'],'materials_match':a['materials']==b['materials'],'method':'Triangles canonicalized per mesh/material, cyclic order retained; position/normal/UV rounded to 1e-6. Image embedded bytes and material texture content compared.'};Path(sys.argv[3]).write_text(json.dumps(r,indent=2));print(json.dumps({k:r[k]for k in ['byte_identical','geometry_match_1e6','images_match','materials_match']},indent=2))
