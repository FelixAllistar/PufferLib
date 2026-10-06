import bpy,json,math
from pathlib import Path
from mathutils import Vector
O=Path(__file__).resolve().parent;C=json.loads((O.parent/'source_inspection/F/VIEW_DEFINITIONS.json').read_text())['cameras'];bpy.ops.wm.open_mainfile(filepath=str(O/'support_grip_index_release_c1.editable.blend'));s=bpy.context.scene;s.frame_set(0);s.render.engine='BLENDER_WORKBENCH';s.display.shading.light='STUDIO';s.display.shading.studiolight_rotate_z=.35;s.display.shading.color_type='MATERIAL';s.display.shading.show_shadows=True;s.display.shading.show_cavity=True;s.display.shading.cavity_type='BOTH';s.display.shading.background_type='WORLD';s.world.color=(.075,.085,.10);s.render.resolution_x=900;s.render.resolution_y=900;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG'
for ob in bpy.data.objects:
 if ob.type=='ARMATURE' or ob.name=='Rebuild Review Floor':ob.hide_render=True
cam=bpy.data.objects.new('Fixed baseline inspection camera',bpy.data.cameras.new('Fixed baseline inspection camera'));s.collection.objects.link(cam);s.camera=cam;out=O/'views';out.mkdir(exist_ok=True)
for name,p in C.items():
 if name not in ['left_inner','left_outer','left_below','near_visor_inspection']: continue
 cam.location=Vector(p['location']);target=Vector(p['target']);cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO' if p['orthographic_scale_m'] else 'PERSP';cam.data.ortho_scale=p['orthographic_scale_m']or .26;cam.data.lens=36/(2*math.tan(math.radians(80)/2));cam.data.sensor_width=36;cam.data.clip_start=.005;cam.data.clip_end=20;s.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)
(out/'CAMERA_IDENTITY.json').write_text(json.dumps({'cameras_exact_to':'source_inspection/F/VIEW_DEFINITIONS.json','values':C,'limits':'Near-visor camera remains uncalibrated; no engine camera or anatomical eye assumption'},indent=2)+'\n')
