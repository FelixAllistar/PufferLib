"""Neutral Blender GLB reimport views of the exported rigid weapon assets."""
import bpy,math,sys
from pathlib import Path
from mathutils import Vector

root=Path(__file__).resolve().parent
package=Path(sys.argv[sys.argv.index('--')+1])if '--'in sys.argv else root/'package'
views=package/'views';views.mkdir(exist_ok=True)
def setup(filename):
    bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(package/filename));s=bpy.context.scene
    s.render.engine='CYCLES';s.cycles.device='CPU';s.cycles.samples=48;s.cycles.use_denoising=False
    s.render.resolution_x=1200;s.render.resolution_y=700;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG'
    s.world=bpy.data.worlds.new('Neutral studio');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.22,.22,.22,1);s.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.65
    cdata=bpy.data.cameras.new('Neutral camera');c=bpy.data.objects.new('Neutral camera',cdata);s.collection.objects.link(c);s.camera=c
    for name,pos,power,size in [('Key',(.2,-1.5,2),500,2),('Fill',(.8,1.3,1),450,2),('Rim',(-.8,.5,.8),350,1.5)]:
        ld=bpy.data.lights.new(name,'AREA');ld.energy=power;ld.shape='DISK';ld.size=size;l=bpy.data.objects.new(name,ld);s.collection.objects.link(l);l.location=pos;l.rotation_euler=(Vector((.45,0,0))-l.location).to_track_quat('-Z','Y').to_euler()
    return s,c
def render(s,c,name,pos,target,scale=1.08,perspective=False):
    c.location=pos;c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler();c.data.type='PERSP'if perspective else 'ORTHO';c.data.ortho_scale=scale;c.data.lens=55;c.data.clip_start=.005
    s.render.filepath=str(views/name);bpy.ops.render.render(write_still=True)
s,c=setup('rifle7_rigid_textured.glb')
render(s,c,'rifle_side.png',(.45,-2,.015),(.45,0,.015))
render(s,c,'rifle_top.png',(.45,0,2),(.45,0,0))
render(s,c,'rifle_threequarter.png',(1.15,-1.4,.55),(.43,0,.015),1.08)
s.render.resolution_x=900;s.render.resolution_y=900
render(s,c,'rifle_sight_line.png',(-.30,0,.12474874),(.65497363,0,.12472590),perspective=True)
s,c=setup('sidearm50_rigid_source_scale.glb');s.render.resolution_x=1000;s.render.resolution_y=700
for o in bpy.data.objects:
    if o.type=='LIGHT':o.location*=3;o.data.energy*=5;o.data.size*=3
render(s,c,'sidearm_source_scale.png',(4,-5,2),(1.15,0,-.12),3.2)
print('NEUTRAL_VIEWS_DONE',flush=True)
