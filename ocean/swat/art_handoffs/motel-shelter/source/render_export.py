"""Render a fresh import of the actual runtime GLB. Review stage never enters asset files."""
import bpy, math, json, os, hashlib
from pathlib import Path
from mathutils import Vector
ROOT=Path(os.environ.get('SHELTER_RENDER_ROOT',Path(__file__).resolve().parent.parent))
bpy.ops.wm.read_factory_settings(use_empty=True)
p=ROOT/'runtime'/'BC_SHELTER01_static.glb';bpy.ops.import_scene.gltf(filepath=str(p))
assets=[o for o in bpy.data.objects if o.type=='MESH']
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=int(os.environ.get('SHELTER_SAMPLES','64'));s.cycles.use_denoising=False
s.render.resolution_x=1400;s.render.resolution_y=1100;s.render.resolution_percentage=int(os.environ.get('SHELTER_RES','100'));s.view_settings.view_transform='AgX'
s.world=bpy.data.worlds.new('Review neutral world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.29,.33,.32,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.5
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,0.0));floor=bpy.context.object;floor.name='REVIEW_ONLY_ground'
m=bpy.data.materials.new('REVIEW_ONLY_ground');m.diffuse_color=(.22,.235,.225,1);floor.data.materials.append(m)
def aim(o,t):o.rotation_euler=(Vector(t)-o.location).to_track_quat('-Z','Y').to_euler()
for n,pos,power,size,col in [('Key',(-3,-4,6),1600,5,(1,.93,.83)),('Fill',(4,-2,4),1100,5,(.85,.92,1)),('Rim',(1,4,5),1500,4,(1,.98,.90))]:
    d=bpy.data.lights.new(n,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=col;o=bpy.data.objects.new(n,d);bpy.context.collection.objects.link(o);o.location=pos;aim(o,(0,0,1.1))
c=bpy.data.cameras.new('ReviewCamera');cam=bpy.data.objects.new('ReviewCamera',c);bpy.context.collection.objects.link(cam);s.camera=cam
views=[
    ('front_threequarter',(4.4,-5.7,3.25),(0,0,1.18),'ORTHO',4.45),
    ('rear_threequarter',(-4.4,5.7,3.2),(0,0,1.18),'ORTHO',4.45),
    ('front',(0,-6,1.75),(0,0,1.18),'ORTHO',4.05),
    ('eye_level',(3.5,-5.7,1.67),(0,.05,1.15),'PERSP',47),
    ('bench_join_detail',(2.9,-3.1,1.18),(.65,.27,.52),'ORTHO',1.90),
    ('ground_contact',(3.5,-3.8,.65),(1.37,-.57,.12),'ORTHO',.73),
    ('roof_drain_detail',(3.8,4.0,3.15),(1.30,.71,2.14),'ORTHO',.94),
    ('rear',(0,6,1.80),(0,0,1.18),'ORTHO',4.05),
]
out=ROOT/'previews';out.mkdir(exist_ok=True)
receipt={'runtime_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'method':'Fresh GLB import with review-only floor/lights/camera. No source-scene rendering; no postpaint.','render_engine':'Blender 4.3.2 Cycles','views':[]}
only=os.environ.get('SHELTER_VIEW')
if only:
    names=only.split(',');views=sorted([v for v in views if v[0] in names],key=lambda v:names.index(v[0]))
    if (ROOT/'qa'/'render_binding.json').exists():
        old=json.loads((ROOT/'qa'/'render_binding.json').read_text())
        if old['runtime_sha256']==receipt['runtime_sha256']:receipt['views']=[v for v in old['views'] if v['name'] not in names]
for name,pos,target,kind,val in views:
    cam.location=pos;c.type=kind
    if kind=='ORTHO':c.ortho_scale=val
    else:c.lens=val
    aim(cam,target);s.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)
    receipt['views'].append({'name':name,'file':'previews/'+name+'.png','sha256':hashlib.sha256((out/(name+'.png')).read_bytes()).hexdigest(),'camera_blender_xyz_m':pos,'target_blender_xyz_m':target,'camera_type':kind,'orthographic_width_m' if kind=='ORTHO' else 'lens_mm':val,'render_resolution':[int(s.render.resolution_x*s.render.resolution_percentage/100),int(s.render.resolution_y*s.render.resolution_percentage/100)]})
(ROOT/'qa'/'render_binding.json').write_text(json.dumps(receipt,indent=2))
