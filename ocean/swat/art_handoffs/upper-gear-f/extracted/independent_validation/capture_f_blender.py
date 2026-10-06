"""Independent read-only Blender capture for F. Never saves source or import scene.
blender -b --python capture_f_blender.py -- source.blend out-prefix
Captures every original key plus every half-key at 120Hz for the three-second proof.
"""
import bpy, numpy as np, json, hashlib, sys, re
from pathlib import Path
src,out=map(Path,sys.argv[sys.argv.index('--')+1:]);src=src.resolve();out=out.resolve();pin=hashlib.sha256(src.read_bytes()).hexdigest()
if src.suffix=='.blend':bpy.ops.wm.open_mainfile(filepath=str(src))
else:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(src))
rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE'];assert len(rigs)==1
rig=rigs[0];scene=bpy.context.scene;fps=scene.render.fps/scene.render.fps_base
names=[p.name for p in rig.pose.bones]
meshes=sorted([o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)],key=lambda o:o.name)
assert len(names)==72 and len(meshes)==9,(len(names),[o.name for o in meshes])
arrays={'bone_names':np.array(names),'bone_rest':np.array([rig.matrix_world@b.matrix_local for b in rig.data.bones]),'rig_world':np.array(rig.matrix_world)};metadata=[]
for i,o in enumerate(meshes):
 m=o.data;m.calc_loop_triangles()
 arrays[f'p{i}']=np.array([o.matrix_world@v.co for v in m.vertices]);arrays[f'tris{i}']=np.array([t.vertices[:] for t in m.loop_triangles]);arrays[f'tri_loops{i}']=np.array([t.loops[:] for t in m.loop_triangles]);arrays[f'tri_mat{i}']=np.array([t.material_index for t in m.loop_triangles]);arrays[f'loop_normal{i}']=np.array([x.vector[:] for x in m.corner_normals]);arrays[f'loop_vertex{i}']=np.array([l.vertex_index for l in m.loops])
 for ui,uv in enumerate(m.uv_layers):arrays[f'uv{i}_{ui}']=np.array([x.uv[:] for x in uv.data])
 w=np.zeros((len(m.vertices),len(names)),dtype=np.float64)
 for v in m.vertices:
  for g in v.groups:
   n=o.vertex_groups[g.group].name
   if n in names:w[v.index,names.index(n)]=g.weight
 arrays[f'w{i}']=w
 metadata.append({'name':o.name,'vertices':len(m.vertices),'triangles':len(m.loop_triangles),'materials':[x.name if x else None for x in m.materials],'uv_names':[x.name for x in m.uv_layers]})

def curves(a):
 if hasattr(a,'fcurves'):return list(a.fcurves)
 return [f for layer in a.layers for strip in layer.strips for bag in strip.channelbags for f in bag.fcurves]
def find_action(label):
 target={'neutral':'Neutral Carry / Anatomical Gear F','motion':'Upper Gear F / Three-second Carry Articulation'}[label]
 match=[a for a in bpy.data.actions if a.name==target or a.name.endswith(target) or target in a.name]
 if len(match)!=1:
  match=[a for a in bpy.data.actions if ('neutral' in a.name.lower() if label=='neutral' else 'articulation' in a.name.lower() or 'raise' in a.name.lower())]
 assert len(match)==1,(label,[a.name for a in bpy.data.actions])
 return match[0]
acts={}
for label,times in [('neutral',np.array([0.])),('motion',np.linspace(0,3,361))]:
 a=find_action(label);rig.animation_data.action=a
 if hasattr(a,'slots') and a.slots and hasattr(rig.animation_data,'action_slot'):rig.animation_data.action_slot=a.slots[0]
 arrays[label+'_times']=times;arrays[label+'_bones']=np.empty((len(times),len(names),4,4),dtype=np.float32)
 for i,o in enumerate(meshes):arrays[f'{label}_eval{i}']=np.empty((len(times),len(o.data.vertices),3),dtype=np.float32)
 for ti,t in enumerate(times):
  f=t*fps;scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
  arrays[label+'_bones'][ti]=[rig.matrix_world@p.matrix for p in rig.pose.bones]
  for i,o in enumerate(meshes):
   ev=o.evaluated_get(dg);m=ev.to_mesh();assert len(m.vertices)==len(o.data.vertices);arr=np.empty((len(m.vertices)*3,),dtype=np.float32);m.vertices.foreach_get('co',arr);arr=arr.reshape(-1,3);mw=np.array(ev.matrix_world,dtype=np.float32);arrays[f'{label}_eval{i}'][ti]=arr@mw[:3,:3].T+mw[:3,3];ev.to_mesh_clear()
  if ti%60==0:print('CAPTURE',label,ti,len(times),flush=True)
 cs=curves(a);acts[label]={'name':a.name,'frame_range':list(a.frame_range),'curves':len(cs),'keys':sum(len(f.keyframe_points) for f in cs),'sample_count':len(times),'fps':fps}
for k in list(arrays):
 if '_eval' in k or k.endswith('_bones'):arrays[k]=np.moveaxis(arrays[k],0,-1)
np.savez_compressed(str(out)+'.npz',**arrays)
meta={'schema':'independent-upper-gear-f-blender-capture/1','time_axis_last':True,'source_filename':src.name,'source_sha256':pin,'blender_version':bpy.app.version_string,'bone_names':names,'bone_parents':{b.name:b.parent.name if b.parent else None for b in rig.data.bones},'mesh_names':[o.name for o in meshes],'meshes':metadata,'actions':acts,'read_only_source_hash_preserved':hashlib.sha256(src.read_bytes()).hexdigest()==pin,'capture_npz_sha256':hashlib.sha256(Path(str(out)+'.npz').read_bytes()).hexdigest()}
Path(str(out)+'.json').write_text(json.dumps(meta,indent=2)+'\n');print('DONE',out,flush=True)
