"""Independent raw GLB numerical checks; stdlib only. Fails on unsupported geometry."""
import struct,json,math,hashlib
from pathlib import Path
R=Path(__file__).resolve().parents[1];p=R/'runtime/motel_service_trolley.glb';b=p.read_bytes()
magic,v,total=struct.unpack_from('<4sII',b);assert (magic,v,total)==(b'glTF',2,len(b))
n,t=struct.unpack_from('<II',b,12);j=json.loads(b[20:20+n]);off=20+n;bn,bt=struct.unpack_from('<II',b,off);blob=b[off+8:off+8+bn]
def acc(i):
 a=j['accessors'][i];vw=j['bufferViews'][a['bufferView']];k={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']];f={5126:'f',5125:'I',5123:'H',5121:'B'}[a['componentType']];sz=struct.calcsize(f)*k;o=vw.get('byteOffset',0)+a.get('byteOffset',0);stride=vw.get('byteStride',sz)
 return [struct.unpack_from('<'+f*k,blob,o+x*stride) for x in range(a['count'])]
ps=[];tri=0;badarea=0;badnormal=0;normerr=0;uvbad=0
for mesh in j['meshes']:
 for pr in mesh['primitives']:
  assert pr.get('mode',4)==4
  a=pr['attributes'];assert all(k in a for k in ['POSITION','NORMAL','TANGENT','TEXCOORD_0']);pos=acc(a['POSITION']);no=acc(a['NORMAL']);uv=acc(a['TEXCOORD_0']);ix=[x[0] for x in acc(pr['indices'])]
  assert max(ix)<len(pos) and min(ix)>=0 and len(ix)%3==0
  ps+=pos;tri+=len(ix)//3
  for n in no:normerr=max(normerr,abs(sum(x*x for x in n)-1))
  for row in uv:uvbad+=any(x<0 or x>1 for x in row)
  for k in range(0,len(ix),3):
   ia,ib,ic=ix[k:k+3];u=[pos[ib][d]-pos[ia][d]for d in range(3)];v=[pos[ic][d]-pos[ia][d]for d in range(3)];cr=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];area=math.sqrt(sum(x*x for x in cr))/2
   badarea+=area<1e-14;badnormal+=any(sum(cr[d]*no[ii][d]for d in range(3))<-1e-12 for ii in [ia,ib,ic])
assert all(math.isfinite(x)for i in range(len(j['accessors']))for row in acc(i)for x in row)
print("Diagnostics",tri,badarea,badnormal,uvbad,normerr);assert not (badarea or badnormal or uvbad);assert normerr<1e-5
assert all(all(k not in nd for k in ['matrix','translation','rotation','scale']) for nd in j['nodes'])
assert len(j['materials'])==1 and j['materials'][0].get('alphaMode','OPAQUE')=='OPAQUE'
assert len(j['images'])==2 and all('bufferView' in im for im in j['images'])
for imcheck in j['images']:
 vwcheck=j['bufferViews'][imcheck['bufferView']];pic=blob[vwcheck.get('byteOffset',0):vwcheck.get('byteOffset',0)+vwcheck['byteLength']]
 assert pic[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II',pic,16)==(512,512)
assert 'metallicRoughnessTexture' in j['materials'][0]['pbrMetallicRoughness']
im=j['images'][0];vw=j['bufferViews'][im['bufferView']];png=blob[vw.get('byteOffset',0):vw.get('byteOffset',0)+vw['byteLength']];assert png[:8]==b'\x89PNG\r\n\x1a\n';w,h=struct.unpack_from('>II',png,16);assert (w,h)==(512,512)
lo=[min(v[k]for v in ps)for k in range(3)];hi=[max(v[k]for v in ps)for k in range(3)]
r={'file':p.name,'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'triangles':tri,'vertices':len(ps),'primitive_count':sum(len(m['primitives']) for m in j['meshes']),'materials':1,'alpha_mode':'OPAQUE','embedded_basecolor_images':1,'embedded_metallic_roughness_images':1,'atlas_px':[w,h],'identity_nodes':True,'coordinate_system':'metres, Y up, front +Z, ground Y=0','bounds_min_m':lo,'bounds_max_m':hi,'dimensions_xyz_m':[hi[k]-lo[k] for k in range(3)],'indices_valid':True,'degenerate_triangles':badarea,'opposed_vertex_normals':badnormal,'maximum_normal_unit_error':normerr,'out_of_range_uvs':uvbad,'finite_accessors':True,'tested_triangle_budget':[1000,2500]}
assert 1000<=tri<=2500
(R/'qa/runtime_audit.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
