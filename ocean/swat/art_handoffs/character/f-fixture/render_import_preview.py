"""Two static Blender reimport sanity views; not an engine playback test."""
import bpy, sys
from pathlib import Path
from mathutils import Vector

root=Path(__file__).resolve().parent;package=root/'package'
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.render.fps=24;scene.render.fps_base=1
bpy.ops.import_scene.gltf(filepath=str(package/'standing_f_original_timing.glb'))
rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
action=next(a for a in bpy.data.actions if a.name.startswith('Standing Empty / Coordinated Contact Cleanup F'))
for track in list(rig.animation_data.nla_tracks):rig.animation_data.nla_tracks.remove(track)
rig.animation_data.action=action
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
for label,t in [('ready',0.),('seated',3.65)]:
    frame=t*24;scene.frame_set(int(frame),subframe=frame%1)
    scene.render.filepath=str(package/f'reimport_{label}.png')
    bpy.ops.render.render(write_still=True)
print('REIMPORT_PREVIEWS_DONE',flush=True)
