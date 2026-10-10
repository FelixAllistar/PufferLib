"""Render current exported GLB without modifying source or runtime."""
import bpy,json,hashlib
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(R/'runtime/motel_fire_extinguisher.glb'))
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=48;s.cycles.use_denoising=False;s.render.resolution_x=1000;s.render.resolution_y=1000;s.render.resolution_percentage=100
s.world=bpy.data.worlds.new('Neutral_world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.17,.19,.20,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.55
s.view_settings.view_transform='AgX';s.view_settings.exposure=0
subject=[x for x in s.objects if x.type=='MESH'];vs=[x.matrix_world@v.co for x in subject for v in x.data.vertices];lo=[min(v[i] for v in vs)for i in range(3)];hi=[max(v[i] for v in vs)for i in range(3)]
(R/'qa/reimport.json').write_text(json.dumps({'subject':'Actual GLB freshly imported','runtime_sha256':hashlib.sha256((R/'runtime/motel_fire_extinguisher.glb').read_bytes()).hexdigest(),'mesh_count':len(subject),'blender_bounds_min':lo,'blender_bounds_max':hi},indent=2))
for loc,power,size in [((-1.2,-1.8,2.0),150,1.2),((1.5,-.8,.8),95,1.0),((0,.7,1.8),110,.8)]:
 bpy.ops.object.light_add(type='AREA',location=loc);a=bpy.context.object;a.data.energy=power;a.data.size=size;a.rotation_euler=(Vector((0,-.12,.27))-a.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add();c=bpy.context.object;s.camera=c;c.data.type='ORTHO'
for name,loc,target,scale in [('full',(.85,-1.7,.85),(0,0,.27),.72),('front',(0,-1.7,.55),(0,0,.27),.67),('close',(.38,-1,.68),(0,-.01,.475),.29),('rear',(.75,1.5,.85),(0,0,.27),.72)]:
 c.location=loc;c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler();c.data.ortho_scale=scale;s.render.filepath=str(R/'preview'/f'{name}.png');bpy.ops.render.render(write_still=True)
