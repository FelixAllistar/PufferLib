"""Export the existing pistol at its imported scale, without inventing dimensions.

blender -b --python export_sidearm.py -- SOURCE.fbx.bin LANDMARKS.json OUTPUT_DIRECTORY
"""
import bpy,json,hashlib,sys
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector

source,landmarks,out=map(Path,sys.argv[sys.argv.index('--')+1:]);out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before=sha(source);assert before=='afad5811b629a9c2abcabdd12dcc812d33bbb0d175375fa3719e3b5193299048'
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(source.resolve()),use_image_search=False)
obj=next(o for o in bpy.data.objects if o.type=='MESH');obj.data.calc_loop_triangles();mesh=obj.data
V=np.array([v.co for v in mesh.vertices]);TR=np.array([list(t.vertices)for t in mesh.loop_triangles]);M=np.array(obj.matrix_world)
L=json.loads(landmarks.read_text());features=L['other_useful_components'];slide=next(x for x in features if x['component_id']==18);bore=next(x for x in features if x['component_id']==1)
origin=np.array([slide['local_bounds_min'][0],bore['local_vertex_centroid'][1],bore['local_vertex_centroid'][2]])
x=M[:3,0]/np.linalg.norm(M[:3,0]);y=M[:3,1];y=y-x*np.dot(x,y);y/=np.linalg.norm(y);z=np.cross(x,y);R=np.stack([x,y,z],axis=1)
T=np.eye(4);T[:3,:3]=R.T@M[:3,:3];T[:3,3]=-T[:3,:3]@origin
# glTF -> Blender coordinates before export.
C=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1.]])
N=C@T
vertices=(N@np.c_[V,np.ones(len(V))].T).T[:,:3]
np.savez_compressed(out/'Sidearm50_RigidBody_source.npz',vertices=V,triangles=TR,
                    triangle_loop_indices=np.array([list(t.loops)for t in mesh.loop_triangles]),
                    corner_normals=np.array([n.vector[:]for n in mesh.corner_normals]),
                    loop_vertex_indices=np.array([l.vertex_index for l in mesh.loops]),
                    uv=np.array([l.uv[:]for l in mesh.uv_layers.active.data]))
new=bpy.data.meshes.new('Sidearm50_ExplicitTriangles');new.from_pydata(vertices.tolist(),[],TR.tolist());new.update()
uv=new.uv_layers.new(name='UVMap');normal_matrix=np.linalg.inv(N[:3,:3]).T;normals=[]
for dst,tri in zip(new.polygons,mesh.loop_triangles):
    dst.use_smooth=mesh.polygons[tri.polygon_index].use_smooth
    for dl,sl in zip(dst.loop_indices,tri.loops):
        uv.data[dl].uv=mesh.uv_layers.active.data[sl].uv;n=normal_matrix@np.array(mesh.corner_normals[sl].vector);normals.append(n/np.linalg.norm(n))
new.normals_split_custom_set(normals)
for mat in mesh.materials:new.materials.append(mat)
oldname=obj.name;obj.data=new;obj.name='Sidearm50_SourceScale';obj.matrix_world=Matrix.Identity(4)
bpy.context.view_layer.objects.active=obj;obj.select_set(True)
settings={'export_format':'GLB','use_selection':True,'export_animations':False,'export_skins':False,'export_cameras':False,'export_lights':False,'export_yup':True,'export_tangents':True}
path=out/'sidearm50_rigid_source_scale.glb';bpy.ops.export_scene.gltf(filepath=str(path.resolve()),**settings)
def convert(point):return (T@np.r_[point,1])[:3].tolist()
tip=L['muzzle_tip']
tip_center=tip.get('local_center',tip.get('local_tip_center'))
if tip_center is None:
    ids=[i for i in range(1792)if V[i,0]>=V[:1792,0].max()-1e-6];tip_center=V[ids].mean(0).tolist()
front=next(a for a in L['front_sight']if a['component_id']==12)['local_vertex_centroid']
rear=np.mean([a['local_vertex_centroid']for a in L['rear_sight']if a['component_id']in (13,14)],axis=0)
grip_ids=[i for i in range(5891,len(V))if V[i,0]<-.7 and V[i,1]<-.5]
grip=(V[grip_ids].min(0)+V[grip_ids].max(0))/2
points=[{'id':'origin_rear_slide_axial_reference','position_export_engine':convert(origin),'method':'Rear slide axial station at the measured bore-component center Y/Z'},
        {'id':'muzzle','position_export_engine':convert(tip_center),'position_native':list(tip_center),'method':'Mean of foremost 32-vertex muzzle-extension tip ring'},
        {'id':'rear_sight','position_export_engine':convert(rear),'position_native':list(rear),'method':'Midpoint of two rear sight dot/inset component centroids; candidate sight alignment'},
        {'id':'front_sight','position_export_engine':convert(front),'position_native':list(front),'method':'Front sight dot/inset component centroid; candidate sight alignment'},
        {'id':'grip_region_center','position_export_engine':convert(grip),'position_native':list(grip),'method':'Frame component 19 vertices with native X < -0.7, Y < -0.5; bounding-box center','source_vertex_ids':grip_ids}]
E=(T@np.c_[V,np.ones(len(V))].T).T[:,:3]
report={'schema':'swat-rigid-sidearm-recipe/1','source_file':source.name,'source_sha256':before,'source_object':oldname,
        'blender_version':bpy.app.version_string,'source_imported_matrix_row_major':M.tolist(),
        'native_to_export_engine_row_major':T.tolist(),'export_engine_to_native_row_major':np.linalg.inv(T).tolist(),
        'orientation':'Source local +X muzzle/+Y up aligned to engine +X forward/+Y up/+Z right',
        'scale_policy':'Preserve imported metric geometry. Only remove source scene translation and rigid orientation. Intended physical scale is unverified; no resize to canonical 0.36 m.',
        'source_baked_object_axis_scales':[float(np.linalg.norm(M[:3,i]))for i in range(3)],
        'additional_uniform_scale':1.0,'bounds_export_engine':[E.min(0).tolist(),E.max(0).tolist()],
        'dimensions_export_engine':np.ptp(E,axis=0).tolist(),'origin_native':origin.tolist(),
        'nodes':['Sidearm50_SourceScale'],'vertices':len(V),'triangles':len(TR),
        'textures':0,'materials':'Original untextured material preserved; no source images found',
        'settings':settings,'export_file':path.name,'export_sha256':sha(path),'export_bytes':path.stat().st_size,
        'source_unchanged':sha(source)==before}
bindings={'schema':'swat-rigid-sidearm-bindings/1','asset':path.name,'asset_sha256':sha(path),'units':'imported meters; physical scale unverified',
          'axes':{'forward':'+X','up':'+Y','right':'+Z'},'landmarks':points,'stock':'not applicable to pistol',
          'right_hand_binding':'not calibrated; geometric grip region only','support_hand_binding':'not calibrated; no source hand rig or anatomical contact measurement',
          'canonical_sidearm_profile_barrel_m':.36,'measured_origin_to_muzzle_m':float(np.linalg.norm(np.array(convert(tip_center))))}
(out/'sidearm_recipe.json').write_text(json.dumps(report,indent=2));(out/'sidearm_bindings.json').write_text(json.dumps(bindings,indent=2))
assert report['source_unchanged'];print('SIDEARM_EXPORTED',json.dumps(report),flush=True)
