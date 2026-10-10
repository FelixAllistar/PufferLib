"""Render current exported GLB without modifying source or runtime."""
import bpy,json
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(R/'runtime/motel_reception_noticeboard.glb'))
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=48;s.cycles.use_denoising=False;s.render.resolution_x=1000;s.render.resolution_y=1000;s.render.resolution_percentage=100
s.world=bpy.data.worlds.new('Neutral_world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.17,.19,.20,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.55
s.view_settings.view_transform='AgX';s.view_settings.exposure=-.9
subject=[x for x in s.objects if x.type=='MESH'];vs=[x.matrix_world@v.co for x in subject for v in x.data.vertices];lo=[min(v[i] for v in vs)for i in range(3)];hi=[max(v[i] for v in vs)for i in range(3)]
(R/'qa/reimport.json').write_text(json.dumps({'subject':'Actual GLB freshly imported','runtime_sha256':__import__('hashlib').sha256((R/'runtime/motel_reception_noticeboard.glb').read_bytes()).hexdigest(),'mesh_count':len(subject),'blender_bounds_min':lo,'blender_bounds_max':hi},indent=2))
for loc,power,size in [((-.5,-.7,.7),40,.7),((.5,-.4,.2),16,.5),((0,.4,.5),20,.5)]:
 bpy.ops.object.light_add(type='AREA',location=loc);a=bpy.context.object;a.data.energy=power;a.data.size=size;a.rotation_euler=(Vector((0,-.025,0))-a.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add();c=bpy.context.object;s.camera=c;c.data.type='ORTHO'
for name,loc,target,scale in [('rear',(.40,1,.30),(0,0,0),.85),('full',(.50,-1.3,.48),(0,-.015,0),.86),('front',(0,-1,0),(0,-.025,0),.82),('detail',(-.16,-1,.10),(-.14,-.025,.055),.46)]:
 c.location=loc;c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler();c.data.ortho_scale=scale;s.render.filepath=str(R/'preview'/f'{name}.png');bpy.ops.render.render(write_still=True)
