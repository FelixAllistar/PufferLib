"""Independent read-only source/reimport capture. Run inside stock Blender.
blender -b --factory-startup --python capture_blender.py -- source.blend output-prefix
blender -b --factory-startup --python capture_blender.py -- asset.glb output-prefix
Never saves the opened source or imported scene.
"""
import sys,json,hashlib
from pathlib import Path
import bpy,numpy as np
source,out=map(Path,sys.argv[sys.argv.index('--')+1:]); source=source.resolve();out=out.resolve();h=hashlib.sha256(source.read_bytes()).hexdigest()
if source.suffix=='.blend':bpy.ops.wm.open_mainfile(filepath=str(source))
else:
 bpy.ops.wm.read_factory_settings(use_empty=True)
 bpy.ops.import_scene.gltf(filepath=str(source))
rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE'); scene=bpy.context.scene
scene.frame_set(0);bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();a=rig.animation_data.action
# Blender 4.4+ layered actions and classic source actions.
if hasattr(a,'fcurves'):curves=list(a.fcurves)
else:curves=[f for layer in a.layers for strip in layer.strips for bag in strip.channelbags for f in bag.fcurves]
bones=list(rig.pose.bones);bn=[b.name for b in bones];arrays={'bone_names':np.array(bn),'bone_world':np.array([rig.matrix_world@b.matrix for b in bones]),'bone_rest':np.array([rig.matrix_world@b.bone.matrix_local for b in bones]),'rig_world':np.array(rig.matrix_world)}
mesh_names=[];meshes=[]
for o in sorted(bpy.context.scene.objects,key=lambda o:o.name):
 if o.type!='MESH' or not any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers):continue
 i=len(mesh_names);mesh_names.append(o.name);m=o.data;m.calc_loop_triangles();ev=o.evaluated_get(dg);em=ev.to_mesh()
 arrays[f'p{i}']=np.array([o.matrix_world@v.co for v in m.vertices]);arrays[f'local_p{i}']=np.array([v.co for v in m.vertices]);arrays[f'world{i}']=np.array(o.matrix_world)
 arrays[f'eval{i}']=np.array([ev.matrix_world@v.co for v in em.vertices]);arrays[f'tris{i}']=np.array([t.vertices[:] for t in m.loop_triangles]);arrays[f'mat{i}']=np.array([t.material_index for t in m.loop_triangles]);arrays[f'loop_vert{i}']=np.array([l.vertex_index for l in m.loops]);arrays[f'loop_normal{i}']=np.array([l.normal[:] for l in m.loops])
 for li,uv in enumerate(m.uv_layers):arrays[f'uv{i}_{li}']=np.array([v.uv[:] for v in uv.data])
 w=np.zeros((len(m.vertices),len(bn)));vg={g.index:g.name for g in o.vertex_groups}
 for v in m.vertices:
  for g in v.groups:
   if vg[g.group] in bn:w[v.index,bn.index(vg[g.group])]=g.weight
 arrays[f'w{i}']=w
 meshes.append({'name':o.name,'native_vertices':len(m.vertices),'evaluated_vertices':len(em.vertices),'triangles':len(m.loop_triangles),'materials':[x.name for x in m.materials],'uv_names':[x.name for x in m.uv_layers],'max_skin_influences':int(np.count_nonzero(w,axis=1).max()),'modifiers':[x.type for x in o.modifiers]});ev.to_mesh_clear()
cur=[{'path':f.data_path,'component':f.array_index,'keys':[[float(k.co[0]),float(k.co[1]),k.interpolation] for k in f.keyframe_points]} for f in curves]
r={'schema':'independent-blender-static-capture/1','source_filename':source.name,'source_sha256':h,'blender_version':bpy.app.version_string,'frame':0,'action':a.name,'action_frame_range':list(a.frame_range),'curves':cur,'curve_count':len(cur),'key_count':sum(len(f['keys']) for f in cur),'rig_name':rig.name,'rig_world':np.array(rig.matrix_world).tolist(),'bone_names':bn,'bone_parents':{b.name:b.parent.name if b.parent else None for b in bones},'bone_count':len(bones),'mesh_names':mesh_names,'meshes':meshes,'read_only_source_hash_preserved':hashlib.sha256(source.read_bytes()).hexdigest()==h}
np.savez_compressed(str(out)+'.npz',**arrays);r['capture_npz_sha256']=hashlib.sha256(Path(str(out)+'.npz').read_bytes()).hexdigest();Path(str(out)+'.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ['source_sha256','action','action_frame_range','curve_count','key_count','bone_count','mesh_names','read_only_source_hash_preserved']},indent=2))
