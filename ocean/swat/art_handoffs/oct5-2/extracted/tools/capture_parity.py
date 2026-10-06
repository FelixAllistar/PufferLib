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
assert source_hash == 'b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9'
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
times=np.array([0.]);np.save(work/'sample_times.npy',times);control_indices=[0]
assert list(rig.animation_data.action.frame_range)==[0,0]
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

# A single STEP key has no interpolation tangent to lose. Stock import is the actual roundtrip.
import shutil
shutil.copy2(work/'stock_reimport.npz',work/'reimport.npz')
report={'source_sha256':source_hash,'glb_sha256':glb_hash,'samples':1,'sample_scope':'One authored static pose at key time0; no authored duration or motion','bone_count':len(bone_names),'mesh_names':mesh_names,'control_indices':[0],'control_times_s':[0.],'reimport_action':action.name,'reimport_method':'Unmodified stock Blender4.3.2 glTF importer; direct one-key STEP action sampling','stock_reimport_method':'Same direct stock single-key import','reimport_frames':list(action.frame_range),'parent_identity':np.array_equal(src['parents'],stock['parents']),'reimport_deformation_max_element_error':float(abs(src['deformation']-stock['deformation']).max()),'scope':'Static exported/reimported skin parity; not a loop, artistic or engine acceptance'}
(work/'capture_report.json').write_text(json.dumps(report,indent=2)+'\n')
assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash and hashlib.sha256(glb.read_bytes()).hexdigest()==glb_hash
print('STATIC_PARITY_CAPTURE_DONE',json.dumps(report),flush=True)
