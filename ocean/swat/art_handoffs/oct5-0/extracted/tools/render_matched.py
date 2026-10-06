"""Matched static neutral views. Usage: blender -b --python render_matched.py -- source|stock_reimport|cubic_reimport INPUT OUTPUT_DIR. Source mode writes native_phaseNNN. Cubic reimport uses explicit spline evaluation, while stock reimport retains the documented importer failure. No asset files are modified."""
import bpy, sys
from pathlib import Path
from mathutils import Vector

mode,input_file,outdir=sys.argv[sys.argv.index('--')+1:]
package=Path(outdir);package.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.render.fps=30;scene.render.fps_base=1
if mode=='source':
    bpy.ops.wm.open_mainfile(filepath=str(Path(input_file).resolve()))
    scene=bpy.context.scene;scene.render.fps=30;scene.render.fps_base=1
    rig=bpy.data.objects['SWAT_Mixamo_Rig']
    keep={rig.name}|{o.name for o in bpy.data.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)}
    for obj in list(bpy.data.objects):
        if obj.name not in keep:bpy.data.objects.remove(obj,do_unlink=True)
    for obj in bpy.data.objects:obj.hide_render=False;obj.hide_viewport=False;obj.hide_set(False)
else:
    assert mode in ['stock_reimport','cubic_reimport']
    bpy.ops.import_scene.gltf(filepath=str(Path(input_file).resolve()))
rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
action=next(a for a in bpy.data.actions if a.name.startswith('Walk Left / Shared Ready N C1 Loop A'))
for track in list(rig.animation_data.nla_tracks):rig.animation_data.nla_tracks.remove(track)
rig.animation_data.action=action
scene.view_settings.view_transform='AgX';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=16
scene.cycles.use_denoising=False
scene.render.resolution_x=600;scene.render.resolution_y=700;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.world=bpy.data.worlds.new('Fixture preview world');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.15,.15,.15,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.7
camera_data=bpy.data.cameras.new('Fixture preview camera');camera=bpy.data.objects.new(camera_data.name,camera_data)
scene.collection.objects.link(camera);camera.location=(3.5,-5.5,2.6)
camera.rotation_euler=(Vector((0,-.05,.95))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=2.35;scene.camera=camera
for name,location,power,size in [('Key',(3,-4,5),600,4),('Fill',(-3,-1,3),350,3),('Rim',(0,3,4),500,3)]:
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size
    obj=bpy.data.objects.new(name,data);scene.collection.objects.link(obj);obj.location=location
    obj.rotation_euler=(Vector((0,0,1))-obj.location).to_track_quat('-Z','Y').to_euler()
if mode=='cubic_reimport':
    import numpy as np
    from mathutils import Matrix
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    from gltf_math import GLB
    parsed=GLB(Path(input_file));doc=parsed.doc;names={n.get('name'):i for i,n in enumerate(doc['nodes'])};skin=doc['skins'][0];slots={n:i for i,n in enumerate(skin['joints'])};ibm=parsed.acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1);C=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1.]])
    bones=list(rig.pose.bones);rest={b.name:np.array(b.bone.matrix_local)for b in bones};R=np.array(rig.matrix_world);RI=np.linalg.inv(R)
    rig.animation_data.action=None
for label,t in [('phase000',0.),('phase950',.95)]:
    frame=t*30;scene.frame_set(int(frame),subframe=frame%1)
    if mode=='cubic_reimport':
        W=parsed.world_at(doc['animations'][0],[t])[0];target={b.name:RI@C@W[names[b.name]]@ibm[slots[names[b.name]]]@C.T@R@rest[b.name]for b in bones}
        for b in bones:
            basis=np.linalg.inv(rest[b.name])@target[b.name]if b.parent is None else np.linalg.inv(rest[b.name])@rest[b.parent.name]@np.linalg.inv(target[b.parent.name])@target[b.name]
            b.matrix_basis=Matrix(basis.tolist())
        bpy.context.view_layer.update()
    prefix='native'if mode=='source'else mode
    scene.render.filepath=str(package/f'{prefix}_{label}.png')
    bpy.ops.render.render(write_still=True)
print('MATCHED_PREVIEWS_DONE',flush=True)
