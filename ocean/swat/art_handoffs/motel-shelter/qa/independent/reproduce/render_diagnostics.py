import bpy,json,hashlib
from pathlib import Path
from mathutils import Vector
D=Path(__file__).parent;P=D/'snapshot/runtime/BC_SHELTER01_static.glb';O=D/'diagnostics';O.mkdir(exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(P))
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=48;s.cycles.use_denoising=False;s.render.resolution_x=1200;s.render.resolution_y=900;s.render.resolution_percentage=100;s.view_settings.view_transform='AgX'
s.world=bpy.data.worlds.new('Audit_world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.42,.42,.42,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.7

def aim(o,t):o.rotation_euler=(Vector(t)-o.location).to_track_quat('-Z','Y').to_euler()
for name,pos,power,size in [('Front',(1,-3,4),1000,4),('Back',(-2,3,4),1300,4),('Under',(0,-2,-1),700,3)]:
 d=bpy.data.lights.new(name,'AREA');d.energy=power;d.size=size;o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);o.location=pos;aim(o,(0,0,1))
c=bpy.data.cameras.new('AuditCamera');o=bpy.data.objects.new('AuditCamera',c);bpy.context.collection.objects.link(o);s.camera=o;c.type='ORTHO'
views=[('seat_underside',(3,-4,-1),(0,.20,.42),3.35),('drain_opening',(1.43,.82,3.3),(1.43,.77,2.15),.26),('gutter_end_inside',(-2,2.2,3.0),(-1.55,.77,2.18),.42)]
r={'runtime_sha256':hashlib.sha256(P.read_bytes()).hexdigest(),'method':'Independent fresh GLB import, neutral lights and cameras; no floor or asset geometry edits.','views':[]}
for name,pos,target,scale in views:
 o.location=pos;c.ortho_scale=scale;aim(o,target);s.render.filepath=str(O/(name+'.png'));bpy.ops.render.render(write_still=True);r['views'].append({'file':name+'.png','sha256':hashlib.sha256((O/(name+'.png')).read_bytes()).hexdigest(),'camera_blender_xyz_m':pos,'target_blender_xyz_m':target,'orthographic_width_m':scale})
(O/'binding.json').write_text(json.dumps(r,indent=2))
