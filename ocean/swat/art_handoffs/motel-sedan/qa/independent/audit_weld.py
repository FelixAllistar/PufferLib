#!/usr/bin/env python3
import sys,json,struct,hashlib
from pathlib import Path
from collections import Counter
import numpy as np
p=Path(sys.argv[1]);data=p.read_bytes();pos=12
while pos<len(data):
 n,t=struct.unpack_from('<II',data,pos);pos+=8;c=data[pos:pos+n];pos+=n
 if t==0x4e4f534a:j=json.loads(c)
 else:b=c
DT={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'};NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}
def a(i):
 ac=j['accessors'][i];v=j['bufferViews'][ac['bufferView']];dt=np.dtype(DT[ac['componentType']]);nc=NC[ac['type']];return np.ndarray((ac['count'],nc),dtype=dt,buffer=b,offset=v.get('byteOffset',0)+ac.get('byteOffset',0),strides=(v.get('byteStride',nc*dt.itemsize),dt.itemsize)).copy()
r=[]
for m in j['meshes']:
 edge=Counter();faces=Counter();directed=Counter();volume=0;coords={};idx=0;vv=[]
 for p in m['primitives']:
  v=a(p['attributes']['POSITION']);f=a(p['indices']).reshape(-1,3)
  for t in f:
   ns=[]
   for k in t:
    q=tuple(np.rint(v[k]*1e6).astype('int64'))
    if q not in coords:coords[q]=idx;idx+=1;vv.append(v[k])
    ns.append(coords[q])
   faces[tuple(sorted(ns))]+=1
   for x,y in zip(ns,ns[1:]+ns[:1]):edge[tuple(sorted((x,y)))]+=1;directed[(x,y)]+=1
   v0,v1,v2=v[t].astype(float);volume+=np.dot(v0,np.cross(v1,v2))/6
 winding=[]
 for (x,y),n in edge.items():
  if directed[(x,y)]!=directed[(y,x)]:winding.append([int(x),int(y),directed[(x,y)],directed[(y,x)]])
 r.append({'mesh':m['name'],'welded_vertices_1e6':len(coords),'triangles':sum(faces.values()),'duplicate_geometric_triangles':sum(n-1 for n in faces.values()if n>1),'boundary_edges':sum(n==1 for n in edge.values()),'nonmanifold_edges':sum(n!=2 for n in edge.values()),'inconsistent_winding_edges':len(winding),'winding_examples':winding[:10],'signed_volume_m3':volume})
out={'runtime_sha256':hashlib.sha256(data).hexdigest(),'method':'All primitive POSITIONs rounded to 1 micrometre and spatially welded per mesh; edge incidence and oriented edge-pair checks. This is topology QA, not a solid boolean union or engine collision mesh.','meshes':r};Path(sys.argv[2]).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
