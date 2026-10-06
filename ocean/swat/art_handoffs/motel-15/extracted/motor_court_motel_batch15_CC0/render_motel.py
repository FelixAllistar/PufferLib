"""Deterministic CPU catalogue and four actual geometry views; no denoiser."""
import bpy,os,sys
from mathutils import Vector
ROOT=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,ROOT)
import build_motel as b
from build_motel import g
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'))
g.MATS.update({k:bpy.data.materials['MC15_'+k] for k in b.PALETTE if 'MC15_'+k in bpy.data.materials})
sc=bpy.context.scene;sc.render.threads_mode='FIXED';sc.render.threads=2;sc.cycles.device='CPU';sc.cycles.use_denoising=False
def frame_cutaway(cam):
 bpy.context.view_layer.update();roof=bpy.data.collections['02b_ROOFS_toggle_cutaway'];objects=[o for o in bpy.data.collections['02_MOTEL_EXAMPLE'].all_objects if o.type=='MESH' and not o.get('presentation_only') and o.name not in roof.objects];inv=cam.matrix_world.inverted();pts=[inv@(o.matrix_world@Vector(p)) for o in objects for p in o.bound_box];lo=[min(p[i] for p in pts) for i in [0,1]];hi=[max(p[i] for p in pts) for i in [0,1]];cam.location+=cam.matrix_world.to_quaternion()@Vector(((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,0));cam.data.ortho_scale=max(hi[0]-lo[0],(hi[1]-lo[1])*sc.render.resolution_x/sc.render.resolution_y)*1.12;bpy.context.view_layer.update()
if '--exteriors-only' in sys.argv:
 roof=bpy.data.collections['02b_ROOFS_toggle_cutaway'];sc.render.resolution_x=1400;sc.render.resolution_y=1000;sc.cycles.samples=32
 cam=bpy.data.objects['01_Courtyard'];cam.data.lens=32;sc.camera=cam;roof.hide_render=False;sc.render.filepath=os.path.join(ROOT,'motel_courtyard.png');bpy.ops.render.render(write_still=True)
 cam=bpy.data.objects['02_Cutaway'];frame_cutaway(cam);sc.camera=cam;roof.hide_render=True;sc.render.filepath=os.path.join(ROOT,'motel_cutaway.png');bpy.ops.render.render(write_still=True)
 bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'));raise SystemExit(0)
src=bpy.data.collections['01_NEW_ASSET_LIBRARY'];old=bpy.data.collections['01b_REUSED_LIBRARY'];assembly=bpy.data.collections['02_MOTEL_EXAMPLE'];roof=bpy.data.collections['02b_ROOFS_toggle_cutaway'];st=bpy.data.collections['02a_STRUCTURE'];pres=bpy.data.collections['03_CAMERAS_LIGHTS']
for ob in list(pres.objects):bpy.data.objects.remove(ob,do_unlink=True)
src.hide_viewport=False;old.hide_viewport=False
if '--locations-only' not in sys.argv:
 assembly.hide_render=True;old.hide_render=True;src.hide_render=False
 obs=[bpy.data.objects[n] for n,_,_ in b.BUILDERS]
 for ob in obs:ob.hide_render=True
 stage=g.stage_box('Catalogue_floor',(0,0,-.20),(50,50,.1),'concrete',pres)
 mat=bpy.data.materials.new('Presentation_neutral_ground');mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.19,.22,.22,1);bs.inputs['Roughness'].default_value=1;stage.data.materials[0]=mat
 lamps=[g.light(pres,'Catalogue_key',(-5,-6,8),(0,0,1),1800,6,(1,.92,.82)),g.light(pres,'Catalogue_fill',(5,-1,6),(0,0,1),950,5,(.75,.84,1))]
 cam=g.camera(pres,'Catalogue',(4,-6,4),(0,0,1),4)
 sc.render.resolution_x=400;sc.render.resolution_y=400;sc.cycles.samples=24
 for i,ob in enumerate(obs):
  ob.hide_render=False;bb=g.bounds(ob);stage.location.z=bb[0][2]+.145;mid=Vector([(bb[0][j]+bb[1][j])/2 for j in range(3)]);extent=max(bb[1][j]-bb[0][j] for j in range(3));cam.location=mid+Vector((2.5,-4.6,2.6))*extent;cam.rotation_euler=(mid-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=extent*1.5
  sc.render.filepath=os.path.join(ROOT,'previews','%02d_%s.png'%(i+1,ob.name));bpy.ops.render.render(write_still=True);ob.hide_render=True
 for ob in [stage,cam]+lamps:bpy.data.objects.remove(ob,do_unlink=True)
src.hide_render=True;old.hide_render=True;assembly.hide_render=False
if '--catalog-only' not in sys.argv:
 g.light(pres,'Overcast_softbox',(-6,-10,17),(-2,2,0),24000,14,(1,.94,.82));g.light(pres,'North_fill',(6,9,15),(0,1,0),17000,14,(.77,.85,1))
 for x in [-10,-6,-2,2,6]:g.light(pres,'Room_ceiling_pool',(x,2.25,2.53),(x,2.25,0),115,2,(1,.85,.64))
 for x in [-6,-2,2,6]:g.light(pres,'Bathroom_pool',(x,5.05,2.53),(x,5.05,0),65,1.5,(1,.92,.77))
 g.light(pres,'Reception_rear_pool',(-10,4.9,2.53),(-10,4.9,0),100,1.7,(1,.89,.72))
 g.light(pres,'Laundry_pool',(-10,8,2.53),(-10,8,0),150,2,(.86,.93,1))
 sc.render.resolution_x=1400;sc.render.resolution_y=1000;sc.cycles.samples=32
 roof.hide_render=False;g.camera(pres,'01_Courtyard',(15,-20,7.2),(-2,-1.5,.7),None,32);sc.render.filepath=os.path.join(ROOT,'motel_courtyard.png');bpy.ops.render.render(write_still=True)
 roof.hide_render=True;cam=g.camera(pres,'02_Cutaway',(7,-8,43),(-2,2,.0),30);frame_cutaway(cam);sc.render.filepath=os.path.join(ROOT,'motel_cutaway.png');bpy.ops.render.render(write_still=True)
 roof.hide_render=False;sc.cycles.samples=80;sc.render.resolution_x=1120;sc.render.resolution_y=800
 g.camera(pres,'03_Guest_room',(-7.15,.67,1.62),(-5.74,2.93,1.05),None,22);sc.render.filepath=os.path.join(ROOT,'motel_guest_room.png');bpy.ops.render.render(write_still=True)
 g.camera(pres,'04_Reception',(-11.10,.78,1.67),(-9.68,4.85,1.22),None,22);sc.render.filepath=os.path.join(ROOT,'motel_reception.png');bpy.ops.render.render(write_still=True)
 sc.camera=bpy.data.objects['02_Cutaway'];roof.hide_render=True
src.hide_viewport=True;old.hide_viewport=True
for screen in bpy.data.screens:
 for area in screen.areas:
  if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA'
if '--catalog-only' not in sys.argv:bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'))
