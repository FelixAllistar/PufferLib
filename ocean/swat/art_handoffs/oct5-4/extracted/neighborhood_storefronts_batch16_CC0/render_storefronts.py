"""Inspectable Blender CPU renders, two threads, original scene geometry only."""
import bpy,os,sys,math
from mathutils import Vector
ROOT=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,ROOT)
import build_storefronts as b
from build_storefronts import g
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'neighborhood_storefronts.blend'))
g.MATS.update({key:bpy.data.materials['NS16_'+key] for key in b.PALETTE if 'NS16_'+key in bpy.data.materials})
sc=bpy.context.scene;sc.render.threads_mode='FIXED';sc.render.threads=2;sc.cycles.device='CPU';sc.cycles.use_denoising=False
src=bpy.data.collections['01_NEW_ASSET_LIBRARY'];old=bpy.data.collections['01b_REUSED_LIBRARY'];assembly=bpy.data.collections['02_STOREFRONTS_EXAMPLE'];roof=bpy.data.collections['02b_ROOFS_toggle_cutaway'];pres=bpy.data.collections['03_CAMERAS_LIGHTS']
for ob in list(pres.objects):bpy.data.objects.remove(ob,do_unlink=True)
src.hide_viewport=False;old.hide_viewport=False
if '--locations-only' not in sys.argv and '--draft' not in sys.argv and '--cutaway-only' not in sys.argv:
 assembly.hide_render=True;old.hide_render=True;src.hide_render=False
 obs=[bpy.data.objects[n] for n,_,_ in b.BUILDERS]
 for ob in obs:ob.hide_render=True
 stage=g.stage_box('Catalogue_floor',(0,0,-.20),(60,60,.1),'concrete',pres)
 mat=bpy.data.materials.new('Presentation_neutral_ground');mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.17,.205,.22,1);bs.inputs['Roughness'].default_value=1;stage.data.materials[0]=mat
 lamps=[g.light(pres,'Catalogue_key',(-5,-6,8),(0,0,1),1500,6,(1,.94,.85)),g.light(pres,'Catalogue_fill',(5,-1,6),(0,0,1),1050,5,(.78,.88,1))]
 cam=g.camera(pres,'Catalogue',(4,-6,4),(0,0,1),4)
 sc.render.resolution_x=360;sc.render.resolution_y=360;sc.cycles.samples=20
 for i,ob in enumerate(obs):
  ob.hide_render=False;bb=g.bounds(ob);stage.location.z=bb[0][2]+.145;mid=Vector([(bb[0][j]+bb[1][j])/2 for j in range(3)]);extent=max(bb[1][j]-bb[0][j] for j in range(3));cam.location=mid+Vector((2.5,-4.6,2.6))*extent;cam.rotation_euler=(mid-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=extent*1.5
  sc.render.filepath=os.path.join(ROOT,'previews','%02d_%s.png'%(i+1,ob.name));bpy.ops.render.render(write_still=True);ob.hide_render=True
 for ob in [stage,cam]+lamps:bpy.data.objects.remove(ob,do_unlink=True)
src.hide_render=True;old.hide_render=True;assembly.hide_render=False
if '--catalog-only' not in sys.argv:
 g.light(pres,'Sky_key',(-7,-9,14),(0,3,0),18000,12,(1,.94,.84));g.light(pres,'North_fill',(9,12,13),(0,4,0),13500,12,(.77,.85,1))
 for x in [-3,3]:
  for y in [2.1,4.7]:g.light(pres,'Ceiling_pool',(x,y,3.05),(x,y,0),160,2,(.94,.97,1) if x<0 else (1,.88,.71))
 for x in [-4,0,4]:g.light(pres,'Service_pool',(x,8.5,3.04),(x,8.5,0),150,1.6,(.95,.97,1))
 draft='--draft' in sys.argv;sc.render.resolution_x=800 if draft else 1120;sc.render.resolution_y=560 if draft else 784;sc.cycles.samples=16 if draft else 80;sc.cycles.use_adaptive_sampling=True;sc.cycles.adaptive_threshold=.03;sc.cycles.adaptive_min_samples=16 if draft else 32
 views=[('01_street_front',(9,-11,1.72),(-.5,.3,2.0),None,27,False),('02_laundromat',(-2.83,.70,1.65),(-3.45,3.80,1.23),None,20,False),('03_pawn_repair',(3.03,.72,1.65),(3.04,3.50,1.20),None,20,False),('04_shared_service',(-3.05,7.1,1.65),(1.6,9.45,1.24),None,24,False),('05_cutaway',(14,-18,23),(0,4,.75),19.5,35,True)]
 for name,loc,target,ortho,lens,cut in views:
  roof.hide_render=cut;cam=g.camera(pres,name,loc,target,ortho,lens)
  if cut:
   bpy.context.view_layer.update();inv=cam.matrix_world.inverted();pts=[inv@(ob.matrix_world@Vector(v)) for ob in assembly.all_objects if ob.type=='MESH' and not ob.get('presentation_only') and ob.name not in roof.objects for v in ob.bound_box]
   lo=[min(v[j] for v in pts) for j in [0,1]];hi=[max(v[j] for v in pts) for j in [0,1]];cam.location+=cam.matrix_world.to_quaternion()@Vector(((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,0));cam.data.ortho_scale=max(hi[0]-lo[0],(hi[1]-lo[1])*sc.render.resolution_x/sc.render.resolution_y)*1.12;bpy.context.view_layer.update()
  if '--cutaway-only' in sys.argv and not cut:continue
  sc.render.filepath=os.path.join(ROOT,('draft_' if draft else '')+name+'.png');bpy.ops.render.render(write_still=True)
 if not draft:
  sc.camera=bpy.data.objects['05_cutaway'];roof.hide_render=True
  src.hide_viewport=True;old.hide_viewport=True
  for screen in bpy.data.screens:
   for area in screen.areas:
    if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA'
  bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'neighborhood_storefronts.blend'))
