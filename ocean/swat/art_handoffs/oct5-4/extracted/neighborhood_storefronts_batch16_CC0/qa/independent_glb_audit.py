"""Read-only independent binary glTF QA. Run with OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2."""
import os
os.environ['OMP_NUM_THREADS']='2';os.environ['OPENBLAS_NUM_THREADS']='2'
import hashlib,io,json,struct
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
DT={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}
WIDTH={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def matrix(n):
 if 'matrix' in n:return np.asarray(n['matrix'],float).reshape(4,4).T
 x,y,z,w=n.get('rotation',[0,0,0,1]);m=np.eye(4);m[:3,:3]=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])@np.diag(n.get('scale',[1,1,1]));m[:3,3]=n.get('translation',[0,0,0]);return m
def audit(path,rec=None,kind='new'):
 errs=[];warn=[]
 def ck(ok,msg):
  if not ok:errs.append(msg)
 raw=path.read_bytes();magic,version,length=struct.unpack_from('<III',raw);ck((magic,version,length)==(0x46546c67,2,len(raw)),'Invalid GLB header');chunks={};off=12
 while off<len(raw):
  ln,typ=struct.unpack_from('<II',raw,off);ck(ln%4==0 and off+8+ln<=len(raw),'Invalid chunk bounds');chunks[typ]=raw[off+8:off+8+ln];off+=8+ln
 d=json.loads(chunks[0x4e4f534a]);b=chunks[0x004e4942];ck(d['asset']['version']=='2.0','glTF version');ck(not d.get('extensionsRequired'),'Required extensions');ck(not [v['uri'] for k in ['images','buffers'] for v in d.get(k,[]) if 'uri'in v],'External/URI dependencies');ck(len(d['buffers'])==1 and d['buffers'][0]['byteLength']<=len(b),'Embedded buffer invalid')
 for v in d.get('bufferViews',[]):ck(v.get('buffer',0)==0 and v.get('byteOffset',0)+v['byteLength']<=len(b),'Buffer view invalid')
 def ac(i):
  a=d['accessors'][i];v=d['bufferViews'][a['bufferView']];dt=np.dtype(DT[a['componentType']]);n=WIDTH[a['type']];st=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',dt.itemsize*n);assert not a.get('sparse');assert st+(a['count']-1)*stride+dt.itemsize*n<=v.get('byteOffset',0)+v['byteLength'];return np.ndarray((a['count'],n),dtype=dt,buffer=b,offset=st,strides=(stride,dt.itemsize))
 image_rows=[]
 for i,im in enumerate(d.get('images',[])):
  ck('bufferView'in im and im.get('mimeType') in ['image/png','image/jpeg'],'Image not embedded PNG/JPEG')
  v=d['bufferViews'][im['bufferView']];by=b[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']]
  with Image.open(io.BytesIO(by)) as pim:
   pim.load();ck(pim.width>0 and pim.height>0 and len(pim.tobytes())>0,'Empty decoded image');image_rows.append({'index':i,'width':pim.width,'height':pim.height,'bytes':len(by)})
 ck(bool(image_rows),'No embedded textures');mats=d.get('materials',[]);tx=d.get('textures',[]);glass=[]
 for t in tx:ck(0<=t.get('source',-1)<len(image_rows),'Texture source invalid')
 for m in mats:
  p=m.get('pbrMetallicRoughness',{});c=p.get('baseColorFactor',[1,1,1,1]);ck(np.isfinite(c).all() and all(0<=v<=1 for v in c),'Invalid PBR base color')
  for k in ['metallicFactor','roughnessFactor']:ck(np.isfinite(p.get(k,1)) and 0<=p.get(k,1)<=1,'Invalid PBR '+k)
  for v in [p.get('baseColorTexture'),p.get('metallicRoughnessTexture'),m.get('normalTexture'),m.get('emissiveTexture')]:
   if v:ck(0<=v['index']<len(tx),'Invalid material texture reference');ck(v.get('texCoord',0)==0,'Unexpected UV set')
  if kind=='new':
   ck('baseColorTexture' in p,'New material missing basecolor image')
   if m.get('name')=='NS16_glass':
    ck(m.get('alphaMode')=='BLEND' and abs(c[3]-.2)<1e-6 and c[:3]==[1,1,1],'Glass alpha 0.20 contract');ck(not m.get('extensions'),'Glass requires runtime extension');glass.append(m['name'])
   else:ck(m.get('alphaMode','OPAQUE')=='OPAQUE','Unexpected non-glass transparency')
   if 'normalTexture' in m:ck(abs(m['normalTexture'].get('scale',1)-1)<1e-6,'Normal strength differs from 1')
 positions=[];triangles=0;deg=0;normal_err=0;uvdeg=0;mesh_nodes=[];node_transforms={};tangent_count=0;tangent_length_error=0;tangent_normal_dot_max=0;tangent_handedness_error=0
 def visit(ni,parent,seen):
  nonlocal triangles,deg,normal_err,uvdeg,tangent_count,tangent_length_error,tangent_normal_dot_max,tangent_handedness_error
  assert ni not in seen;n=d['nodes'][ni];xf=parent@matrix(n);ck(np.isfinite(xf).all(),'Nonfinite transform');node_transforms[n.get('name',str(ni))]=xf.tolist()
  if 'mesh'in n:
   mesh_nodes.append(n);ex=n.get('extras',{})
   if kind in ['new','example']:
    ck(ex.get('render_only') is True,'Mesh render_only missing: '+n.get('name',''));ck(ex.get('collider_enabled') is False,'Mesh collider_enabled not false: '+n.get('name',''));ck(ex.get('colliders_supplied') is False,'Mesh colliders_supplied not false: '+n.get('name',''))
   if kind=='new':ck(ex.get('units')=='metres' and ex.get('license')=='CC0-1.0','Units/license missing')
   for p in d['meshes'][n['mesh']]['primitives']:
    ck(p.get('mode',4)==4,'Nontriangle primitive');at=p['attributes'];ck('POSITION'in at and 'NORMAL'in at and 'TEXCOORD_0'in at,'Missing positions/normals/UV0');pts=ac(at['POSITION']).astype(float)
    for k,i in at.items():ck(len(ac(i))==len(pts) and np.isfinite(ac(i)).all(),'Invalid attribute '+k)
    if kind in ['new','example'] and 'normalTexture' in mats[p['material']]:ck('TANGENT' in at,'Normal-mapped primitive missing exported tangent basis')
    if 'TANGENT' in at:
     tang=ac(at['TANGENT']).astype(float);norm=ac(at['NORMAL']).astype(float);tangent_count+=len(tang);tle=float(np.max(np.abs(np.linalg.norm(tang[:,:3],axis=1)-1)));tne=float(np.max(np.abs(np.sum(tang[:,:3]*norm,axis=1))));the=float(np.max(np.abs(np.abs(tang[:,3])-1)));tangent_length_error=max(tangent_length_error,tle);tangent_normal_dot_max=max(tangent_normal_dot_max,tne);tangent_handedness_error=max(tangent_handedness_error,the);ck(tle<.001,'Non-unit tangent XYZ on '+n.get('name','?')+' / '+mats[p['material']].get('name','?')+'; maximum length error='+str(tle));ck(tne<.001,'Tangent not orthogonal to normal on '+n.get('name','?')+'; maximum dot='+str(tne));ck(the<1e-6,'Tangent handedness not ±1 on '+n.get('name','?'))
    norms=ac(at['NORMAL']);normal_err=max(normal_err,float(np.max(np.abs(np.linalg.norm(norms,axis=1)-1))));inds=ac(p['indices']).ravel() if 'indices'in p else np.arange(len(pts));ck(len(inds)%3==0 and inds.min()>=0 and inds.max()<len(pts),'Bad indices');tt=pts[inds.reshape(-1,3)];ar=np.linalg.norm(np.cross(tt[:,1]-tt[:,0],tt[:,2]-tt[:,0]),axis=1);deg+=int((ar<1e-14).sum());triangles+=len(tt);positions.append((np.c_[pts,np.ones(len(pts))]@xf.T)[:,:3]);uv=ac(at['TEXCOORD_0'])[inds.reshape(-1,3)];ua=uv[:,1]-uv[:,0];ub=uv[:,2]-uv[:,0];uvdeg+=int((np.abs(ua[:,0]*ub[:,1]-ua[:,1]*ub[:,0])<1e-14).sum())
  for j in n.get('children',[]):visit(j,xf,seen|{ni})
 scene=d['scenes'][d.get('scene',0)]
 if kind in ['new','example']:ck(scene.get('extras',{}).get('batch')=='16_neighborhood_storefronts','Scene batch metadata mismatch')
 for n in scene['nodes']:visit(n,np.eye(4),set())
 pts=np.concatenate(positions);bb=np.array([pts.min(0),pts.max(0)]);ck(deg==0,'Degenerate triangles: '+str(deg));ck(normal_err<.001,'Non-unit normals');ck(not d.get('skins') and not d.get('animations') and not d.get('cameras'),'Unexpected animation/skin/camera');delta=None
 if rec:
  bbs=np.asarray(rec['bounds_m_source_xyz']);expected=np.array([[bbs[0,0],bbs[0,2],-bbs[1,1]],[bbs[1,0],bbs[1,2],-bbs[0,1]]]);delta=float(np.max(np.abs(bb-expected)));ck(delta<.0001,'Manifest bounds mismatch');ck(triangles==rec['triangles'],'Manifest triangles mismatch');ck(len(raw)==rec['bytes'],'Manifest byte size mismatch');ck(hashlib.sha256(raw).hexdigest()==rec['sha256'],'Manifest SHA256 mismatch')
  if kind=='new':ck(len(mats)==rec['material_count'],'Manifest material count mismatch');ck([v.get('extras',{}).get('asset_id') for v in mesh_nodes]==[rec['id']],'Manifest asset ID mismatch')
 if uvdeg:warn.append(str(uvdeg)+' UV-degenerate triangles (review only; thin geometry projection may cause these)')
 return {'file':str(path.relative_to(ROOT)),'kind':kind,'status':'pass' if not errs else 'fail','errors':errs,'warnings':warn,'triangles':triangles,'degenerate_triangles':deg,'uv_degenerate_triangles':uvdeg,'maximum_normal_length_error':normal_err,'tangent_count':tangent_count,'maximum_tangent_length_error':tangent_length_error,'maximum_tangent_normal_dot':tangent_normal_dot_max,'maximum_tangent_handedness_error':tangent_handedness_error,'bounds_gltf_y_up_m':bb.tolist(),'maximum_manifest_bounds_error_m':delta,'material_count':len(mats),'embedded_image_count':len(image_rows),'images':image_rows,'glass_materials':glass,'mesh_instances':len(mesh_nodes),'sha256':hashlib.sha256(raw).hexdigest(),'node_transforms_gltf':node_transforms if kind=='example' else {}}
def main():
 m=json.loads((ROOT/'manifest.json').read_text());rows=[]
 for kind,key in [('new','assets'),('reuse','reused_existing_assets')]:
  for a in m[key]:
   try:rows.append(audit(ROOT/a['file'],a,kind))
   except Exception as e:rows.append({'file':a['file'],'status':'fail','errors':[repr(e)]})
 for f in [m['example']['file'],m['example']['cutaway']]:
  try:rows.append(audit(ROOT/f,kind='example'))
  except Exception as e:rows.append({'file':f,'status':'fail','errors':[repr(e)]})
 errs=[r['file']+': '+e for r in rows for e in r['errors']];out={'status':'pass' if not errs else 'fail','scope':'Independent GLB finite numeric attributes, indexed triangle integrity, unit normals, normal-map tangent basis/normal orthogonality/handedness, embedded decoded images, PBR/glass/render-only contract, metadata, bounds, SHA256 and manifests; no engine runtime QA.','errors':errs,'files':rows};(ROOT/'qa/independent_glb_report.json').write_text(json.dumps(out,indent=2));print(out['status'],json.dumps(errs));return bool(errs)
if __name__=='__main__':raise SystemExit(main())
