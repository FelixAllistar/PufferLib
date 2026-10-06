"""Capture source and reimport deformation for independent GLB skin verification.

blender -b --python capture_parity.py -- SOURCE.blend FIXTURE.glb WORK_DIRECTORY
"""
import bpy, hashlib, json, math, struct, sys
import numpy as np
from pathlib import Path

source, glb, work = map(Path, sys.argv[sys.argv.index('--') + 1:])
work.mkdir(parents=True, exist_ok=True)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
glb_hash = hashlib.sha256(glb.read_bytes()).hexdigest()
assert source_hash == '897771dc55846b4ad2b1181fcb560a55d364a4e22fc5510177b905bbb28ee442'
raw = glb.read_bytes()
size = struct.unpack_from('<I', raw, 12)[0]
doc = json.loads(raw[20:20+size])
binary = raw[28+size:]
def input_times(index):
    a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
    assert a['componentType'] == 5126 and a['type'] == 'SCALAR'
    return np.ndarray((a['count'],), dtype='<f4', buffer=binary,
                      offset=v.get('byteOffset',0)+a.get('byteOffset',0),
                      strides=(v.get('byteStride',4),)).astype(float)

bpy.ops.wm.open_mainfile(filepath=str(source.resolve()))
scene = bpy.context.scene
scene.render.fps, scene.render.fps_base = 30, 1.0
rig = bpy.data.objects['SWAT_Mixamo_Rig']
action_name = rig.animation_data.action.name
bone_names = [b.name for b in rig.data.bones]
bone_index = {n:i for i,n in enumerate(bone_names)}
mesh_names=[o.name for o in bpy.data.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)]
markers=[0,.005,.025,.05,.1,.1249,.125,.25,.5,.75,.875,.9,.95,.975,.995,1]
nominal_duration=1.0
end_time=max(nominal_duration,max(float(k.co.x)/30 for f in rig.animation_data.action.fcurves for k in f.keyframe_points),max(float(input_times(s['input'])[-1]) for s in doc['animations'][0]['samplers']))
times = set(np.linspace(0,nominal_duration,round(nominal_duration*960)+1))
for fc in rig.animation_data.action.fcurves:
    times.update(float(k.co.x)/30 for k in fc.keyframe_points)
for sampler in doc['animations'][0]['samplers']:
    times.update(input_times(sampler['input']))
for t in markers:
    times.update([max(0,t-1e-5),t,min(end_time,t+1e-5)])
for eps in [1/60,1/240,1/480,1/960,1e-3,1e-4,1e-5,1e-6]:times.update([eps,1-eps])
times = np.array(sorted(t for t in times if 0 <= t <= end_time))
assert all(np.any(times==t)for t in np.linspace(0,1,961))
np.save(work/'sample_times.npy',times)
control_indices = sorted(set([0,len(times)//2,len(times)-1]+[int(np.argmin(abs(times-t))) for t in markers]))
def frame(t):
    f=float(t)*30
    scene.frame_set(math.floor(f),subframe=f%1)
def static(r,meshes):
    data={'bone_names':np.array(bone_names),
          'rest':np.array([r.matrix_world@r.data.bones[n].matrix_local for n in bone_names]),
          'parents':np.array([r.data.bones[n].parent.name if r.data.bones[n].parent else '' for n in bone_names])}
    for mi,obj in enumerate(meshes):
        obj.data.calc_loop_triangles()
        data[f'p{mi}']=np.array([obj.matrix_world@v.co for v in obj.data.vertices])
        w=np.zeros((len(obj.data.vertices),len(bone_names)))
        for vertex in obj.data.vertices:
            for group in vertex.groups:
                name=obj.vertex_groups[group.group].name
                if name in bone_index:
                    w[vertex.index,bone_index[name]]=group.weight
        data[f'w{mi}']=w
        data[f'tris{mi}']=np.array([list(t.vertices) for t in obj.data.loop_triangles])
    return data
def capture(label,r,meshes,driver=None):
    data=static(r,meshes); inverse=np.linalg.inv(data['rest'])
    deformation=np.empty((len(times),len(bone_names),4,4))
    for i,t in enumerate(times):
        frame(t)
        if driver is not None:driver(i,r)
        deformation[i]=np.array([r.matrix_world@r.pose.bones[n].matrix for n in bone_names])@inverse
        if i in control_indices:
            for mi,obj in enumerate(meshes):
                evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
                mesh=evaluated.to_mesh()
                data[f'eval_{i}_{mi}']=np.array([obj.matrix_world@v.co for v in mesh.vertices])
                evaluated.to_mesh_clear()
    data['deformation']=deformation
    np.savez_compressed(work/(label+'.npz'),**data)
    print('CAPTURED',label,len(times),flush=True)
    return data

src=capture('source',rig,[bpy.data.objects[n] for n in mesh_names])
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene
scene.render.fps, scene.render.fps_base=30,1.0
bpy.ops.import_scene.gltf(filepath=str(glb.resolve()))
rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
assert set(bone_names)==set(b.name for b in rig.data.bones)
action=next(a for a in bpy.data.actions if a.name.startswith(action_name))
for track in list(rig.animation_data.nla_tracks):
    rig.animation_data.nla_tracks.remove(track)
rig.animation_data.action=action
stock=capture('stock_reimport',rig,[bpy.data.objects[n] for n in mesh_names])
# The stock Blender4.3 importer discards cubic tangents. Retain that measurement,
# then drive the imported skin from explicit glTF cubic evaluation in a separate check.
sys.path.insert(0,str(Path(__file__).resolve().parent))
from gltf_math import GLB
from mathutils import Matrix
parsed=GLB(glb);gdoc=parsed.doc;gn={n.get('name'):i for i,n in enumerate(gdoc['nodes'])};skin=gdoc['skins'][0];slot={n:i for i,n in enumerate(skin['joints'])};ibm=parsed.acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1);W=parsed.world_at(gdoc['animations'][0],times);C=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1.]])
rest=stock['rest'];localrest=np.array([rig.data.bones[n].matrix_local for n in bone_names]);inverse_rest=np.linalg.inv(localrest);Rinv=np.linalg.inv(np.array(rig.matrix_world));parentid=[bone_index[rig.data.bones[n].parent.name]if rig.data.bones[n].parent else None for n in bone_names]
target=np.array([Rinv@C@W[:,gn[n]]@ibm[slot[gn[n]]]@C.T@rest[j]for j,n in enumerate(bone_names)]).transpose(1,0,2,3)
rig.animation_data.action=None
for track in list(rig.animation_data.nla_tracks):rig.animation_data.nla_tracks.remove(track)
def drive(i,r):
 for j,n in enumerate(bone_names):
  parent=parentid[j]
  basis=inverse_rest[j]@target[i,j]if parent is None else inverse_rest[j]@localrest[parent]@np.linalg.inv(target[i,parent])@target[i,j]
  r.pose.bones[n].matrix_basis=Matrix(basis.tolist())
 bpy.context.view_layer.update()
imp=capture('reimport',rig,[bpy.data.objects[n] for n in mesh_names],drive)
report={'source_sha256':source_hash,'glb_sha256':glb_hash,'samples':len(times),
        'sample_scope':'960 Hz plus every source/export key, boundary probes and finite-difference epsilon pairs; source references +/-10 microseconds',
        'bone_count':len(bone_names),'mesh_names':mesh_names,
        'control_indices':control_indices,'control_times_s':[float(times[i]) for i in control_indices],
        'reimport_action':action.name,'reimport_method':'Explicit glTF CUBICSPLINE evaluation driving Blender-imported geometry and bind skeleton; not stock auto-handle reimport','stock_reimport_method':'Unmodified Blender4.3.2 importer; its discarded cubic tangents are measured separately','stock_reimport_deformation_max_error':float(abs(src['deformation']-stock['deformation']).max()),'reimport_frames':list(action.frame_range),
        'parent_identity':np.array_equal(src['parents'],imp['parents']),
        'reimport_deformation_max_element_error':float(abs(src['deformation']-imp['deformation']).max()),
        'scope':'Numerical exported/reimported skin parity, not artistic or engine acceptance'}
(work/'capture_report.json').write_text(json.dumps(report,indent=2))
assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
assert hashlib.sha256(glb.read_bytes()).hexdigest()==glb_hash
print('PARITY_CAPTURE_DONE',json.dumps(report),flush=True)
