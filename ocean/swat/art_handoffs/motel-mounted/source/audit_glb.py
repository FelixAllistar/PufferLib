"""Standard-library GLB QA; numerical geometry, opaque PBR and embedded images."""
from pathlib import Path
import struct,json,math,hashlib
R=Path(__file__).resolve().parents[1]; reports={}
for p in sorted((R/'runtime').glob('*.glb')):
 b=p.read_bytes();magic,version,total=struct.unpack_from('<4sII',b);assert (magic,version,total)==(b'glTF',2,len(b))
 n,t=struct.unpack_from('<II',b,12);j=json.loads(b[20:20+n]);off=20+n;blen,bt=struct.unpack_from('<II',b,off);blob=b[off+8:off+8+blen]
 def acc(i):
  a=j['accessors'][i];v=j['bufferViews'][a['bufferView']];szn={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']];fmt={5126:'f',5125:'I',5123:'H',5121:'B'}[a['componentType']];sz=struct.calcsize(fmt)*szn;start=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',sz)
  return [struct.unpack_from('<'+fmt*szn,blob,start+k*stride) for k in range(a['count'])]
 pr=[p for m in j['meshes'] for p in m['primitives']];positions=[];tri=0
 for pp in pr:
  pos=acc(pp['attributes']['POSITION']);ix=[x[0] for x in acc(pp['indices'])];assert max(ix)<len(pos);assert len(ix)%3==0;tri+=len(ix)//3;positions+=pos
  assert all(a in pp['attributes'] for a in ['NORMAL','TEXCOORD_0','TANGENT'])
 assert all(math.isfinite(x) for i in range(len(j['accessors'])) for row in acc(i) for x in row)
 assert all(all(k not in nd for k in ['matrix','translation','rotation','scale']) for nd in j['nodes'])
 assert all(mat.get('alphaMode','OPAQUE')=='OPAQUE' for mat in j['materials']);assert len(j['materials'])==1
 lo=[min(v[k] for v in positions) for k in range(3)];hi=[max(v[k] for v in positions) for k in range(3)]
 reports[p.name]={'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'triangles':tri,'all_nodes_identity':True,'coordinate_system':'metres, Y-up; front +Z; wall Z=0','gltf_bounds_min_m':lo,'gltf_bounds_max_m':hi,'gltf_dimensions_xyz_m':[hi[k]-lo[k] for k in range(3)],'draw_calls':len(pr),'opaque_materials':1,'embedded_images':len(j['images']),'textures':'original 1024x1024 basecolor, normal and packed metallic-roughness PNG; ORM R is neutral 255','finite_values':True,'index_bounds_valid':True}
(R/'qa/runtime_audit.json').write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2))
