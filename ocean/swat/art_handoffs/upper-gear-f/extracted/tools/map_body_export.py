"""Resolve every exported wearer vertex/triangle to retained F and original N IDs."""
import hashlib,json
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from gltf_math import GLB
r=Path(__file__).resolve().parent;p=r/'package';g=GLB(p/'swat_upper_gear_remake_f_v1.glb');z=np.load(r/'work/native_static.npz');meta=json.loads((r/'work/native_static.json').read_text());mi=meta['mesh_names'].index('SWAT_Wearer');bn=list(z['bone_names']);skin=g.doc['skins'][0];order=[bn.index(g.doc['nodes'][i]['name'])for i in skin['joints']];C=np.array([[1,0,0],[0,0,-1],[0,1,0.]])
source=np.load(p/'body_native_correspondence.npz');info=json.loads((p/'body_native_correspondence.json').read_text());pos=z[f'p{mi}']@C;weights=z[f'w{mi}'][:,order];weights/=weights.sum(1)[:,None];node=next(n for n in g.doc['nodes']if n.get('name')=='SWAT_Wearer');mesh=g.doc['meshes'][node['mesh']];tree=cKDTree(pos);origv=source['F_vertex_to_original_N_vertex'];origp=source['F_polygon_to_original_N_polygon'];sf=source['F_triangle_vertices'];sp=source['F_triangle_polygon'];sm=source['F_polygon_material_slot'];uv=source['F_loop_UV_'+str(info['active_UV_index'])];source_loops=source['F_triangle_loop_indices']
def key(face):
 a,b,c=map(int,face);return min((a,b,c),(b,c,a),(c,a,b))
lookup={}
for i,f in enumerate(sf):lookup.setdefault(key(f),[]).append(i)
arrays={};rows=[];total=0
for pi,pr in enumerate(mesh['primitives']):
 at=pr['attributes'];gp=g.acc(at['POSITION']);gw=np.zeros((len(gp),72));guv=g.acc(at['TEXCOORD_0']);gt=g.acc(pr['indices']).astype(int).reshape(-1,3)
 for suffix in sorted(int(k.split('_')[1])for k in at if k.startswith('WEIGHTS_')):
  j=g.acc(at[f'JOINTS_{suffix}']).astype(int);w=g.acc(at[f'WEIGHTS_{suffix}']).astype(float)
  for c in range(4):np.add.at(gw,(np.arange(len(gp)),j[:,c]),w[:,c])
 gw/=gw.sum(1)[:,None];distance,near=tree.query(gp,k=8);we=np.max(abs(gw[:,None,:]-weights[near]),axis=2);select=np.argmin(distance+we,axis=1);ids=near[np.arange(len(gp)),select];assert np.max(np.linalg.norm(gp-pos[ids],axis=1))<1e-7 and np.max(abs(gw-weights[ids]))<1e-7
 matname=g.doc['materials'][pr['material']]['name'];source_slot=info['source_material_slots'].index(matname);poly=[];maxuv=0.
 for ti,face in enumerate(ids[gt]):
  matches=[i for i in lookup[key(face)]if sm[sp[i]]==source_slot];assert len(matches)==1;i=matches[0];rotation=next(q for q in range(3)if np.array_equal(np.roll(sf[i],-q),face));loop=np.roll(source_loops[i],-rotation);suv=uv[loop].copy();suv[:,1]=1-suv[:,1];maxuv=max(maxuv,float(abs(suv-guv[gt[ti]]).max()));poly.append(sp[i])
 assert maxuv<1e-6;poly=np.array(poly,dtype=np.int32);arrays[f'primitive_{pi}_vertex_to_F']=ids.astype(np.int32);arrays[f'primitive_{pi}_vertex_to_original_N']=origv[ids];arrays[f'primitive_{pi}_triangle_to_F_polygon']=poly;arrays[f'primitive_{pi}_triangle_to_original_N_polygon']=origp[poly];total+=len(poly)
 rows.append({'gltf_primitive':pi,'gltf_material_index':pr['material'],'gltf_material_name':matname,'F_material_slot':source_slot,'original_N_material_slot':source_slot,'original_texture_group':'Ch15_1001'if source_slot==0 else'Ch15_1002','exported_vertices':len(ids),'exported_triangles':len(poly),'unique_original_polygons':len(np.unique(origp[poly])),'max_UV_error_after_standard_V_flip':maxuv,'normalization_scope':'Exact retained original UV/material group; float32 UV encoding tolerance measured. No spec/gloss/normal texture reinterpretation.'})
assert total==len(sf);np.savez_compressed(p/'body_export_correspondence.npz',**arrays)
report={'status':'PASS','glb_sha256':hashlib.sha256(g.raw).hexdigest(),'native_correspondence_sha256':hashlib.sha256((p/'body_native_correspondence.npz').read_bytes()).hexdigest(),'export_correspondence_sha256':hashlib.sha256((p/'body_export_correspondence.npz').read_bytes()).hexdigest(),'wearer_mesh':node['mesh'],'all_retained_triangles_mapped':total,'material_groups':rows,'separate_original_character_texture_package_sha256':'be6b60251053f034761c400eec63b66ea870ac4336f67400eba3a882bdeb1b7f','new_gear_materials':'Separate source-authored polymer/padding factors and planar untextured UVs. No original body texture group is reassigned to those parts.'}
(p/'body_texture_group_correspondence.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
