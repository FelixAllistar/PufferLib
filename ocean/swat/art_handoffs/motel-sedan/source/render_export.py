"""Actual exported runtime reimport previews. Review ground/light/camera excluded."""
import bpy,math,json,os,hashlib
from pathlib import Path
from mathutils import Vector
R=Path(os.environ.get('SEDAN_OUT',Path(__file__).resolve().parent.parent));bpy.ops.wm.read_factory_settings(use_empty=True)
p=R/'runtime/BC_SEDAN01_parked.glb';bpy.ops.import_scene.gltf(filepath=str(p))
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=72;s.cycles.use_denoising=False
s.render.resolution_x=1200;s.render.resolution_y=840;s.render.resolution_percentage=100;s.view_settings.view_transform='AgX'
s.world=bpy.data.worlds.new('Review_neutral_world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.31,.34,.35,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.48
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.004));floor=bpy.context.object;floor.name='REVIEW_ONLY_Ground';m=bpy.data.materials.new('REVIEW_ONLY_Ground');m.diffuse_color=(.19,.205,.21,1);floor.data.materials.append(m)
def aim(o,t):o.rotation_euler=(Vector(t)-o.location).to_track_quat('-Z','Y').to_euler()
for n,pos,pow,size,col in [('Key',(-4,-5,7),2000,5,(1,.94,.85)),('Fill',(4,-1,4),1400,5,(.82,.9,1)),('Rim',(-2,5,6),2300,4,(1,1,.96))]:
 d=bpy.data.lights.new(n,'AREA');d.energy=pow;d.shape='DISK';d.size=size;d.color=col;o=bpy.data.objects.new(n,d);bpy.context.collection.objects.link(o);o.location=pos;aim(o,(0,0,.7))
c=bpy.data.cameras.new('ReviewCamera');cam=bpy.data.objects.new('ReviewCamera',c);bpy.context.collection.objects.link(cam);s.camera=cam;c.type='ORTHO';c.ortho_scale=5.8
views=[('front_threequarter',(-4.9,-6.4,3.3),5.65,(0,0,.70)),('rear_threequarter',(4.9,6.3,3.1),5.65,(0,0,.70)),('front',(0,-7,1.30),3.5,(0,0,.72)),('rear',(0,7,1.30),3.5,(0,0,.72)),('side',(-7,0,1.35),5.15,(0,0,.71)),('eye_level',(-5.5,-5.5,1.45),5.65,(0,0,.71)),('underside',(4,-5,-3),5.65,(0,0,.65)),('wheel_contact',(-3.1,-2.3,.36),1.55,(-.765,-1.28,.315)),('top',(0,-.01,8),6.7,(0,0,.71))]
only=os.environ.get('SEDAN_VIEWS');receipt={'runtime_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'method':'Fresh GLB import. Review-only neutral stage, lights and camera. No retouching.','views':[]}
if only:
 only=only.split(',');views=[v for v in views if v[0] in only]
 rp=R/'qa/render_binding.json'
 if rp.exists():
  old=json.loads(rp.read_text())
  if old['runtime_sha256']==receipt['runtime_sha256']:receipt['views']=[v for v in old['views'] if v['name'] not in only]
for name,pos,scale,t in views:
 floor.hide_render=name=='underside';cam.location=pos;c.ortho_scale=scale;aim(cam,t);s.render.filepath=str(R/'previews'/f'{name}.png');bpy.ops.render.render(write_still=True)
 receipt['views'].append({'name':name,'file':'previews/'+name+'.png','camera_blender_xyz_m':pos,'target_blender_xyz_m':t,'ortho_scale':scale,'sha256':hashlib.sha256((R/'previews'/f'{name}.png').read_bytes()).hexdigest()});(R/'qa/render_binding.json').write_text(json.dumps(receipt,indent=2))
