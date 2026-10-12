"""Render only the MAIN runtime GLB, in neutral front/back and detailed views."""
import bpy,sys,math
from pathlib import Path
from mathutils import Vector
root=Path(sys.argv[sys.argv.index('--')+1]).resolve();bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(root/'runtime/wood_handle_flathead_screwdriver_250mm_MAIN.glb'))
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=32;s.cycles.use_denoising=False;s.render.resolution_x=1000;s.render.resolution_y=1000;s.render.resolution_percentage=100;s.world=bpy.data.worlds.new('Neutral preview world');s.world.color=(.14,.14,.14);s.view_settings.view_transform='AgX';s.view_settings.exposure=-1.5
bpy.ops.object.camera_add();cam=bpy.context.object;cam.data.type='ORTHO';cam.data.clip_start=.001;cam.data.clip_end=100;s.camera=cam
lights=[]
for i in range(3):
 bpy.ops.object.light_add(type='AREA');l=bpy.context.object;l.data.shape='DISK';l.data.size=.3;l.data.energy=[8,5,4][i];lights.append(l)
def aim(o,t):o.rotation_euler=(Vector(t)-o.location).to_track_quat('-Z','Y').to_euler()
views=[('01_front',(0,0,.014),(.09,-.14,.35),.30,1),('02_back',(0,0,.014),(-.09,-.14,-.35),.30,-1),('03_tip',(0,-.109,.012),(.035,-.14,.08),.054,1),('04_collar',(0,-.011,.012),(.065,-.05,.085),.065,1)]
for name,target,loc,scale,side in views:
 cam.location=loc;cam.data.ortho_scale=scale;aim(cam,target)
 for l,loc in zip(lights,[(.13,-.11,.28*side),(-.18,.12,.18*side),(.08,.24,-.12*side)]):l.location=loc;aim(l,target)
 s.render.filepath=str(root/'previews'/f'{name}.png');bpy.ops.render.render(write_still=True)
