"""Verify rigid GLB surface/UV/normal preservation and embedded material payloads."""
import json,hashlib,struct,collections,sys
from pathlib import Path
from io import BytesIO
import numpy as np
from scipy.spatial import cKDTree
from PIL import Image

root=Path(__file__).resolve().parent;p=Path(sys.argv[1])if len(sys.argv)>1 else root/'package'
texture_inputs=Path(sys.argv[2])if len(sys.argv)>2 else root/'texture_inputs'
report={'schema':'swat-rigid-weapon-validation/1','assets':[]}
def inspect(file,recipe_file,source_files):
    raw=(p/file).read_bytes();magic,version,length=struct.unpack_from('<III',raw);assert (magic,version,length)==(0x46546c67,2,len(raw))
    size,kind=struct.unpack_from('<II',raw,12);assert kind==0x4e4f534a;doc=json.loads(raw[20:20+size]);bs,bk=struct.unpack_from('<II',raw,20+size);assert bk==0x004e4942;binary=raw[28+size:28+size+bs]
    assert not doc.get('skins') and not doc.get('animations') and not any('uri'in b for b in doc['buffers'])
    rec=json.loads((p/recipe_file).read_text());T=np.array(rec['native_to_export_engine_row_major']);normalT=np.linalg.inv(T[:3,:3]).T
    def acc(i):
        a=doc['accessors'][i];v=doc['bufferViews'][a['bufferView']];dt=np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']]);w={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        assert not a.get('sparse') and v.get('buffer',0)==0
        data=np.ndarray((a['count'],w),dt,buffer=binary,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',w*dt.itemsize),dt.itemsize)).copy();assert np.isfinite(data).all();return data
    asset={'file':file,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'skins':0,'animations':0,'meshes':[],'embedded_images':[]}
    for node_name,source_file in source_files:
        node=next(n for n in doc['nodes']if n.get('name')==node_name);assert 'matrix'not in node and not any(k in node for k in ('translation','rotation','scale'))
        src=np.load(p/source_file);sourceP=(T@np.c_[src['vertices'],np.ones(len(src['vertices']))].T).T[:,:3]
        st=src['triangles'];sourceUV=src['uv'][src['triangle_loop_indices']].copy();sourceUV[:,:,1]=1-sourceUV[:,:,1]
        sourceN=src['corner_normals'][src['triangle_loop_indices']]@normalT.T;sourceN/=np.linalg.norm(sourceN,axis=2,keepdims=True)
        _,canonical=np.unique(np.round(sourceP,6),axis=0,return_inverse=True)
        def key_and_roll(ids):
            values=[tuple(map(int,np.roll(ids,-k)))for k in range(3)];k=min(range(3),key=lambda n:values[n]);return values[k],k
        lookup=collections.defaultdict(list)
        for i,t in enumerate(st):
            key,roll=key_and_roll(canonical[t]);lookup[key].append((np.roll(sourceUV[i],-roll,axis=0),np.roll(sourceN[i],-roll,axis=0)))
        uv_error=normal_error=position_error=0.;vertices=triangles=0;tangent_error=0;normal_length_error=0;export_points=[]
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            a=primitive['attributes'];assert not any(k.startswith(('JOINTS_','WEIGHTS_'))for k in a);assert all(k in a for k in ['POSITION','NORMAL','TEXCOORD_0','TANGENT'])
            gp=acc(a['POSITION']);gn=acc(a['NORMAL']);gu=acc(a['TEXCOORD_0']);gtan=acc(a['TANGENT']);gt=acc(primitive['indices']).reshape(-1,3)
            assert gt.max()<len(gp);distance,ids=cKDTree(sourceP).query(gp);position_error=max(position_error,float(distance.max()))
            normal_length_error=max(normal_length_error,float(abs(np.linalg.norm(gn,axis=1)-1).max()))
            tangent_error=max(tangent_error,float(abs(np.sum(gtan[:,:3]*gn,axis=1)).max()))
            assert np.allclose(abs(gtan[:,3]),1,atol=1e-6)
            for t in gt:
                key,roll=key_and_roll(canonical[ids[t]]);candidates=lookup[key];assert candidates,('missing surface triangle',node_name,key)
                uv=np.roll(gu[t],-roll,axis=0);normal=np.roll(gn[t],-roll,axis=0)
                k=min(range(len(candidates)),key=lambda i:np.max(abs(uv-candidates[i][0]))+np.max(abs(normal-candidates[i][1])))
                suv,sn=candidates.pop(k);uv_error=max(uv_error,float(abs(uv-suv).max()));normal_error=max(normal_error,float(abs(normal-sn).max()))
            vertices+=len(gp);triangles+=len(gt);export_points.append(gp)
        assert not any(lookup.values());assert triangles==len(st);allp=np.concatenate(export_points)
        row={'node':node_name,'source_vertices':len(sourceP),'source_rendered_vertices':len(set(st.ravel())),'export_vertices_with_seams':vertices,'source_triangles':len(st),'export_triangles':triangles,
             'oriented_surface_triangles_and_uvs_preserved':True,'position_max_error_m':position_error,'uv_max_error':uv_error,'normal_component_max_error':normal_error,
             'normal_length_max_error':normal_length_error,'normal_tangent_dot_max_error':tangent_error,'tangents_present_finite':True,
             'bounds_export_engine':[allp.min(0).tolist(),allp.max(0).tolist()]}
        # Blender custom-normal storage introduces small angular quantization.
        # Report it explicitly; require surface/UV fidelity and <0.1-degree
        # normal-direction deviation instead of claiming literal normal bytes.
        normal_angle_bound_deg=float(np.degrees(2*np.arcsin(min(1,np.sqrt(3)*normal_error/2))))
        row['normal_direction_conservative_bound_deg']=normal_angle_bound_deg
        assert position_error<2e-6 and uv_error<1e-6 and normal_angle_bound_deg<.1 and tangent_error<1e-6,(node_name,row)
        asset['meshes'].append(row)
    for im in doc.get('images',[]):
        assert 'uri'not in im;v=doc['bufferViews'][im['bufferView']];data=binary[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']];image=Image.open(BytesIO(data));image.load()
        row={'name':im.get('name'),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'size':list(image.size),'mode':image.mode}
        if file.startswith('rifle'):
            if 'Metallic-'in im['name']:
                pixels=np.array(image);metal=np.array(Image.open(texture_inputs/'7_uv_checker_material_uv_grid_4096x4096_Metallic.png'));rough=np.array(Image.open(texture_inputs/'7_uv_checker_material_uv_grid_4096x4096_Roughness.png'))
                metal=metal if metal.ndim==2 else metal[:,:,0]
                rough=rough if rough.ndim==2 else rough[:,:,0]
                row['metallic_blue_max_integer_error']=int(abs(pixels[:,:,2].astype(int)-metal.astype(int)).max())
                row['roughness_green_max_integer_error']=int(abs(pixels[:,:,1].astype(int)-rough.astype(int)).max())
                assert row['metallic_blue_max_integer_error']==0 and row['roughness_green_max_integer_error']==0
            else:
                source=texture_inputs/(im['name']+'.png');row['original_png_bytes_identical']=source.read_bytes()==data;assert row['original_png_bytes_identical']
        asset['embedded_images'].append(row)
    asset['materials']=doc.get('materials',[]);asset['status']='PASS';return asset
report['assets'].append(inspect('rifle7_rigid_textured.glb','recipe.json',[('Rifle7_RigidBody','Rifle7_RigidBody_source.npz'),('Rifle7_SeatedMagazine','Rifle7_SeatedMagazine_source.npz')]))
report['assets'].append(inspect('sidearm50_rigid_source_scale.glb','sidearm_recipe.json',[('Sidearm50_SourceScale','Sidearm50_RigidBody_source.npz')]))
report['status']='PASS';report['scope']='Rigid mesh, unit/frame transform, UV, normals/tangents and embedded image validation. No game runtime, lighting calibration, skinning or hand-fit claim.'
(p/'validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
