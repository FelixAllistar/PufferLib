"""C1-only renders using sealed C review's exact camera definitions; no source/pixel changes."""
import bpy,json,sys
from pathlib import Path
from mathutils import Vector
HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(HERE))
from gate_config import ROOT,OUTPUT,CANDIDATE,CANDIDATE_SHA,sha,verify_loaded
ref=ROOT/'grip_review_oct5/candidate_c_review/crease_views/CREASE_VIEW_DEFINITIONS.json';definitions=json.loads(ref.read_text());parameters=definitions['cameras']['oblique'];normal=(Vector(definitions['cameras']['face']['location_world_m'])-Vector(definitions['cameras']['face']['target_world_m'])).normalized();out=OUTPUT/'crease_views';out.mkdir(exist_ok=True);assert sha(CANDIDATE)==CANDIDATE_SHA;bpy.ops.wm.open_mainfile(filepath=str(CANDIDATE));verify_loaded(bpy,'C1');s=bpy.context.scene;s.frame_set(0);bpy.context.view_layer.update();b=bpy.data.objects['SWAT_Wearer'];mapping={q.value:i for i,q in enumerate(b.data.attributes['native_vertex_id'].data)};e=b.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=e.to_mesh();vertices=[e.matrix_world@mesh.vertices[mapping[i]].co for i in [8513,8515,8516]];e.to_mesh_clear()
s.render.engine='BLENDER_WORKBENCH';s.display.shading.light='STUDIO';s.display.shading.studiolight_rotate_z=.35;s.display.shading.color_type='MATERIAL';s.display.shading.show_shadows=True;s.display.shading.show_cavity=True;s.display.shading.cavity_type='BOTH';s.display.shading.background_type='WORLD';s.world.color=(.075,.085,.10);s.render.resolution_x=900;s.render.resolution_y=900;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG'
for ob in bpy.data.objects:ob.hide_render=ob.name!='SWAT_Wearer'
cam=bpy.data.objects.new('Fixed sealed-C crease camera',bpy.data.cameras.new('Fixed sealed-C crease camera'));s.collection.objects.link(cam);s.camera=cam;cam.data.type='ORTHO';cam.data.ortho_scale=parameters['orthographic_scale_m'];cam.data.clip_start=.001;cam.data.clip_end=20;cam.location=Vector(parameters['location_world_m']);cam.rotation_euler=(Vector(parameters['target_world_m'])-cam.location).to_track_quat('-Z','Y').to_euler()
curve=bpy.data.curves.new('External measurement triangle edge','CURVE');curve.dimensions='3D';curve.bevel_depth=.00006;curve.bevel_resolution=2;spl=curve.splines.new('POLY');spl.points.add(3)
for i,v in enumerate(vertices+[vertices[0]]):spl.points[i].co=(*(v+normal*.00008),1)
ob=bpy.data.objects.new('External native-face measurement marker',curve);s.collection.objects.link(ob);mat=bpy.data.materials.new('Measurement orange');mat.diffuse_color=(1,.2,.025,1);curve.materials.append(mat)
for marked in [False,True]:
 ob.hide_render=not marked;s.render.filepath=str(out/('C1_oblique'+('_marked' if marked else '')+'.png'));bpy.ops.render.render(write_still=True)
assert sha(CANDIDATE)==CANDIDATE_SHA;(out/'CREASE_VIEW_IDENTITY.json').write_text(json.dumps({'source':str(CANDIDATE),'sha256':CANDIDATE_SHA,'original_C_definition':str(ref),'original_C_definition_sha256':sha(ref),'exact_camera':parameters,'skin_only_diagnostic':True,'native_face':[8513,8515,8516],'original_C_pixels_and_definitions_unmodified':True},indent=2)+'\n')
