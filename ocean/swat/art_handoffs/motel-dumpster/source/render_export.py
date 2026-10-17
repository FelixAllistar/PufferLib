"""Preview fresh-imported runtime only. Lighting/stage are review-only and never exported."""
import bpy, math, json, os, hashlib
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parent.parent
bpy.ops.wm.read_factory_settings(use_empty=True)
p=R/'runtime'/'BC_DUMP01_closed.glb';bpy.ops.import_scene.gltf(filepath=str(p))
(R/'previews_final').mkdir(exist_ok=True)
assets=[o for o in bpy.data.objects if o.type=='MESH']; bpy.context.scene.render.engine='CYCLES';bpy.context.scene.cycles.samples=96;bpy.context.scene.cycles.use_denoising=False
s=bpy.context.scene;s.render.resolution_x=1200;s.render.resolution_y=1000;s.render.resolution_percentage=100;s.view_settings.view_transform='AgX'
s.world=bpy.data.worlds.new('Review neutral world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.31,.34,.32,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.40
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.008));floor=bpy.context.object;floor.name='REVIEW_ONLY_Ground';m=bpy.data.materials.new('REVIEW_ONLY_Ground');m.diffuse_color=(.22,.24,.22,1);floor.data.materials.append(m)
def aim(o,t):o.rotation_euler=(Vector(t)-o.location).to_track_quat('-Z','Y').to_euler()
for n,pos,pow,size,col in [('Key',(-3,-4,6),1500,4,(1,.93,.82)),('Fill',(4,-1,4),1000,4,(.82,.90,1)),('Rim',(1,4,5),1700,3,(1,1,.95))]:
 d=bpy.data.lights.new(n,'AREA');d.energy=pow;d.shape='DISK';d.size=size;d.color=col;o=bpy.data.objects.new(n,d);bpy.context.collection.objects.link(o);o.location=pos;aim(o,(0,0,.7))
c=bpy.data.cameras.new('ReviewCamera');o=bpy.data.objects.new('ReviewCamera',c);bpy.context.collection.objects.link(o);s.camera=o;c.type='ORTHO';c.ortho_scale=3.2
views=[('front_threequarter',(3,-4,2.65),3.18,(0,0,.78)),('rear_threequarter',(-3.1,3.8,2.55),3.20,(0,0,.78)),('front',(0,-5,1.90),2.98,(0,0,.78)),('right',(4,0,1.90),3.05,(0,0,.88)),('top',(2,-2.2,5.5),4.10,(0,0,.78)),('underside',(2.9,-3.7,-2.3),3.15,(0,0,.76)),('sleeve_skid_detail',(3,-3,1.35),1.45,(.89,-.24,.58))]
receipt={'runtime_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'method':'Each view rendered from fresh runtime GLB import; review plane/lights/camera excluded from asset','views':[]}
only=os.environ.get('DUMP_VIEW')
if only:
 only=only.split(',')
 old=json.loads((R/'qa'/'render_binding.json').read_text());assert old['runtime_sha256']==receipt['runtime_sha256'];receipt['views']=[v for v in old['views'] if v['name'] not in only];views=[v for v in views if v[0] in only]
for n,loc,scale,t in views:
 floor.hide_render=n=='underside';o.location=loc;c.ortho_scale=scale;aim(o,t);s.render.filepath=str(R/'previews_final'/f'{n}.png');bpy.ops.render.render(write_still=True);receipt['views'].append({'name':n,'camera_blender_xyz_m':loc,'target_blender_xyz_m':t,'orthographic_width_m':scale,'file':'previews_final/'+n+'.png','sha256':hashlib.sha256((R/'previews_final'/f'{n}.png').read_bytes()).hexdigest()})
(R/'qa'/'render_binding.json').write_text(json.dumps(receipt,indent=2))
