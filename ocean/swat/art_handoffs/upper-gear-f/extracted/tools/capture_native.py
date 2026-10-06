"""Read-only native F capture at authoring knots, midpoints and selected controls.
Usage: blender -b --python capture_native.py -- SOURCE.blend ACTION OUT_PREFIX static|movement
"""
import bpy,hashlib,json,math,sys
import numpy as np
from pathlib import Path
source,action_name,prefix,mode=sys.argv[sys.argv.index('--')+1:];source=Path(source);prefix=Path(prefix)
expected={'static':'4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0','movement':'7165d1454e0526f01ca85c81f12e0245dfecb22071191918f984477a42c091b1'}
h=hashlib.sha256(source.read_bytes()).hexdigest();assert h==expected[mode]
bpy.ops.wm.open_mainfile(filepath=str(source.resolve()))
rig=bpy.data.objects['SWAT_Mixamo_Rig'];a=bpy.data.actions[action_name];rig.animation_data.action=a
for tr in list(rig.animation_data.nla_tracks):rig.animation_data.nla_tracks.remove(tr)
fps=bpy.context.scene.render.fps/bpy.context.scene.render.fps_base
assert len(a.fcurves)==720 and len(rig.data.bones)==72
curves=[{'path':f.data_path,'component':f.array_index,'extrapolation':f.extrapolation,'keys':[[*k.co,k.interpolation]for k in f.keyframe_points]}for f in a.fcurves]
assert all(not f.modifiers for f in a.fcurves)
if mode=='static':
 assert all(len(f.keyframe_points)==1 and f.keyframe_points[0].co.x==0 for f in a.fcurves);times=np.array([0.]);keys=np.array([0.])
else:
 assert fps==30 and all(len(f.keyframe_points)==181 for f in a.fcurves)
 assert all(k.interpolation=='LINEAR'for f in a.fcurves for k in f.keyframe_points)
 keys=np.arange(181)/60;times=np.array(sorted(set(np.arange(361)/120)|set(keys)|{.001,2.999}))
names=[b.name for b in rig.data.bones];bi={n:i for i,n in enumerate(names)}
meshes=sorted([o for o in bpy.data.objects if o.type=='MESH'and any(m.type=='ARMATURE'and m.object==rig for m in o.modifiers)],key=lambda o:o.name)
data={'bone_names':np.array(names),'rest':np.array([rig.matrix_world@rig.data.bones[n].matrix_local for n in names]),'parents':np.array([rig.data.bones[n].parent.name if rig.data.bones[n].parent else ''for n in names]),'times':times,'key_times':keys,'rig_world':np.array(rig.matrix_world)}
inventory=[]
for i,o in enumerate(meshes):
 m=o.data;m.calc_loop_triangles();data[f'p{i}']=np.array([o.matrix_world@v.co for v in m.vertices]);data[f'tris{i}']=np.array([t.vertices[:]for t in m.loop_triangles]);w=np.zeros((len(m.vertices),72))
 for v in m.vertices:
  for g in v.groups:
   name=o.vertex_groups[g.group].name
   if name in bi:w[v.index,bi[name]]=g.weight
 data[f'w{i}']=w
 inventory.append({'name':o.name,'vertices':len(m.vertices),'triangles':len(m.loop_triangles),'materials':[s.material.name if s.material else None for s in o.material_slots],'world':np.array(o.matrix_world).tolist(),'source_weight_sum_range':[float(w.sum(1).min()),float(w.sum(1).max())]})
control_times=[0]if mode=='static'else[0,.25,.5,.75,1,1.25,1.5,1.75,2,2.25,2.5,2.75,3]
controls=[int(np.argmin(abs(times-t)))for t in control_times]
pose=np.empty((len(times),72,4,4))
for ti,t in enumerate(times):
 f=float(t)*fps;bpy.context.scene.frame_set(math.floor(f),subframe=f%1)
 pose[ti]=np.array([rig.matrix_world@rig.pose.bones[n].matrix for n in names])
 if ti in controls:
  for i,o in enumerate(meshes):
   e=o.evaluated_get(bpy.context.evaluated_depsgraph_get());m=e.to_mesh();data[f'eval_{ti}_{i}']=np.array([o.matrix_world@v.co for v in m.vertices]);e.to_mesh_clear()
data['world']=pose;data['deformation']=pose@np.linalg.inv(data['rest'])
np.savez_compressed(prefix.with_suffix('.npz'),**data)
report={'source_sha256':h,'action':a.name,'fps_context':fps,'frame_range':list(a.frame_range),'authored_duration_s':None if mode=='static'else 3,'stored_span_s':0 if mode=='static'else 3,'mode':mode,'samples':len(times),'times_s':times.tolist(),'key_times_s':keys.tolist(),'control_indices':controls,'control_times_s':[float(times[i])for i in controls],'bone_names':names,'mesh_names':[o.name for o in meshes],'meshes':inventory,'curves':curves,'source_unchanged':hashlib.sha256(source.read_bytes()).hexdigest()==h,'npz_sha256':hashlib.sha256(prefix.with_suffix('.npz').read_bytes()).hexdigest(),'scope':'Actual Blender evaluated source matrices and all-vertex controls; keys60Hz and half-key120Hz plus1ms endpoint diagnostics. Static action has no authored duration.'}
prefix.with_suffix('.json').write_text(json.dumps(report,separators=(',',':'))+'\n');assert report['source_unchanged'];print({k:report[k]for k in ['action','mode','samples','source_sha256','npz_sha256']})
