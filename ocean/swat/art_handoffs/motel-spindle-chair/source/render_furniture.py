"""Render original furniture GLB-source meshes; PNG catalogue assembled separately."""
import bpy, os, math, sys, random
from mathutils import Vector
ROOT=os.path.dirname(os.path.abspath(__file__))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'furniture_variants.blend'))
SC=bpy.context.scene
ASSETS={o['asset_id']:o for o in SC.objects if 'asset_id' in o}
for ob in ASSETS.values():ob.location=(0,0,0);ob.hide_render=True
SC.render.engine='CYCLES';SC.cycles.device='CPU';SC.cycles.samples=32;SC.cycles.use_denoising=False
SC.cycles.max_bounces=5;SC.cycles.diffuse_bounces=3;SC.cycles.glossy_bounces=3
SC.render.image_settings.file_format='PNG';SC.render.resolution_percentage=100
SC.world.color=(.22,.22,.22);SC.view_settings.view_transform='AgX'
SC.view_settings.look='AgX - Medium High Contrast';SC.view_settings.exposure=.6

def mat(name,color,rough=1):
 m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;return m
BASE=mat('PRESENTATION_gray',(.205,.216,.210));WALL=mat('PRESENTATION_plaster',(.32,.323,.297));TRIM=mat('PRESENTATION_trim',(.20,.235,.213));DARK=mat('PRESENTATION_underfloor',(.06,.055,.048))

def cube(name,c,d,m,bev=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=c);o=bpy.context.object;o.name=name;o.dimensions=d;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m)
 if bev:
  mod=o.modifiers.new('Soft edges','BEVEL');mod.width=bev;mod.segments=1
 return o

def light(name,loc,power,size,color=(1,1,1)):
 data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size;data.color=color;o=bpy.data.objects.new(name,data);SC.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((0,0,.5))-o.location).to_track_quat('-Z','Y').to_euler();return o

def cam(loc,target,ortho):
 data=bpy.data.cameras.new('Presentation Camera');data.type='ORTHO';data.ortho_scale=ortho;o=bpy.data.objects.new('Presentation Camera',data);SC.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();SC.camera=o;return o
floor=cube('Studio Ground',(0,0,-.04),(200,200,.07),BASE)
lights=[light('Key',(2.7,-3.7,5.2),750,4.0,(1,.93,.82)),light('Fill',(-3,-1.0,2.7),500,3.5,(.82,.90,1)),light('Rim',(1,3,4.5),650,3,(1,.97,.9))]
camera=cam((4,-6,3.3),(0,0,.5),2.7)
if '--staged-only' not in sys.argv:
 SC.render.resolution_x=640;SC.render.resolution_y=640
 for key,ob in ASSETS.items():
  if '--grounding-refresh' in sys.argv and key not in {'chair_dining_spindle','wardrobe_panel_ajar','laundry_hamper_woven','bedframe_single_institutional','credenza_tambour'}:continue
  ob.hide_render=False
  bb=[ob.matrix_world@Vector(v) for v in ob.bound_box];mn=Vector([min(p[i] for p in bb) for i in range(3)]);mx=Vector([max(p[i] for p in bb) for i in range(3)]);c=(mn+mx)/2;d=mx-mn
  target=c+Vector((0,0,-d.z*.015));camera.location=target+Vector((4,-6,3.2));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=max(d.z*1.35,d.x*1.42,d.y*1.14, .8)
  SC.render.filepath=os.path.join(ROOT,'previews',key+'.png');bpy.ops.render.render(write_still=True);ob.hide_render=True
# Three furnished vignette renders from exactly the same source meshes.
floor.hide_render=True
for o in lights:bpy.data.objects.remove(o,do_unlink=True)
room=[]
room.append(cube('Presentation underfloor',(0,0,-.03),(6.4,5.4,.06),DARK))
rng=random.Random(44)
for j in range(17):
 for i in range(4):
  x=-2.4+i*1.60+(0.0 if j%2 else -.35)
  o=cube('Presentation floorboard',(x,-2.55+j*.32,-.004),(1.584,.314,.027),bpy.data.materials['F04_wood_walnut' if rng.random()<.3 else 'F04_wood_honey'],.002)
  # Real simple UVs for reused packed wood texture.
  uv=o.data.uv_layers.active
  if uv:
   for p in o.data.polygons:
    for li in p.loop_indices:
     v=o.data.vertices[o.data.loops[li].vertex_index].co;uv.data[li].uv=(v.x*3,v.y*2)
  room.append(o)
room.append(cube('Presentation rear wall',(0,2.48,1.45),(6.4,.10,2.90),WALL))
room.append(cube('Presentation left wall',(-3.11,0,1.45),(.10,5.,2.90),WALL))
room.append(cube('Presentation rear baseboard',(0,2.405,.084),(6.2,.04,.165),TRIM,.004))
room.append(cube('Presentation left baseboard',(-3.04,0,.084),(.04,4.85,.165),TRIM,.004))
# Few subtle plaster repairs, no fabricated external imagery.
for x,z,w,h in ((-2.3,.38,.27,.11),(.65,.60,.43,.15),(1.8,1.9,.25,.10),(-.85,1.5,.36,.05)):
 room.append(cube('Presentation plaster patch',(x,2.424,z),(w,.002,h),mat('PRESENTATION_patch_'+str(x),(.27,.286,.264)),.005))
lights=[light('Window daylight',(1.3,-2.0,4.2),950,3.4,(.90,.95,1)),light('Warm bounce',(-1,-1.6,3),350,3.8,(1,.88,.71)),light('Ceiling bounce',(1,1.4,4.5),350,2.8)]
SC.render.resolution_x=1600;SC.render.resolution_y=1200;SC.cycles.samples=64;SC.view_settings.exposure=.3

def show(key,loc,yaw=0):
 ob=ASSETS[key];ob.hide_render=False;ob.location=loc;ob.rotation_euler=(0,0,math.radians(yaw))

def render_scene(name,settings,target,scale):
 for o in ASSETS.values():o.hide_render=True
 for key,loc,yaw in settings:show(key,loc,yaw)
 camera.location=Vector(target)+Vector((5,-7.5,4.6));camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=scale
 SC.render.filepath=os.path.join(ROOT,'previews',name+'.png');bpy.ops.render.render(write_still=True)

render_scene('01_reading_room',[
 ('wingback_armchair_worn',(-1.5,.80,0),-12),('recliner_vinyl_extended',(.60,.3,0),-7),('credenza_tambour',(.8,2.0,0),0),('table_folding_card',(-1.45,-.62,0),12),('chair_dining_spindle',(-2.15,-1.16,0),-35)
],(-.30,.40,.62),5.9)
render_scene('02_bedroom_storage',[
 ('bedframe_single_institutional',(-.76,.47,.0),0),('wardrobe_panel_ajar',(1.0,1.98,0),0),('dresser_low_six_drawer',(-2.45,.63,0),90),('laundry_hamper_woven',(1.36,.66,.0),0)
],(-.30,.62,.72),5.85)
render_scene('03_utility_corner',[
 ('shelving_steel_repaired',(-1.52,2.07,.0),0),('shoe_rack_slatted',(-2.05,.94,.0),90),('chair_metal_folding',(-.85,.75,.0),-20),('table_folding_card',(.7,.15,.0),6),('laundry_hamper_woven',(1.77,1.93,.0),0),('chair_dining_spindle',(.2,-.7,.0),155)
],(-.25,.85,.65),5.8)
print('RENDER_COMPLETE')
