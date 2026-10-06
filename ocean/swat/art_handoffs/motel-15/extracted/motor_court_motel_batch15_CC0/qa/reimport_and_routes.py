"""Fresh imports and sampled capsule routes through actual rendered geometry."""
import bpy,os,json,math
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
m=json.load(open(os.path.join(ROOT,'manifest.json')));errors=[];rows=[]
for a in m['assets']:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT,a['file']));obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];pts=[o.matrix_world@v.co for o in obs for v in o.data.vertices];bb=[[min(v[i] for v in pts) for i in range(3)],[max(v[i] for v in pts) for i in range(3)]];tri=0
 for o in obs:o.data.calc_loop_triangles();tri+=len(o.data.loop_triangles)
 checks={'triangles':tri==a['triangles'],'bounds':max(abs(bb[k][j]-a['bounds_m_source_xyz'][k][j]) for k in [0,1] for j in range(3))<.0001,'images':bool(bpy.data.images) and all(len(im.pixels)>0 and im.has_data and im.packed_file for im in bpy.data.images),'render_only':all(not o.rigid_body and o.get('collider_enabled') is False and o.get('render_only') is True for o in obs)}
 if not all(checks.values()):errors.append(a['id']+' import check failed')
 rows.append({'id':a['id'],'checks':checks,'triangles':tri})
examples=[]
for f in ['motel_example.glb','motel_example_cutaway.glb']:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT,f));obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];checks={'scene_provenance':bpy.context.scene.get('batch')=='15_motor_court_motel','images':all(len(im.pixels)>0 and im.has_data and im.packed_file for im in bpy.data.images),'disabled_physics':all(not o.rigid_body and o.get('collider_enabled') is False for o in obs),'instances':len(obs)>130}
 if not all(checks.values()):errors.append(f+' import check failed')
 examples.append({'file':f,'mesh_instances':len(obs),'packed_images':len(bpy.data.images),'checks':checks})
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'));ass=bpy.data.collections['02_MOTEL_EXAMPLE'];src=bpy.data.collections['01_NEW_ASSET_LIBRARY'];src.hide_viewport=False;bpy.context.view_layer.update()
native={'scene_provenance':bpy.context.scene.get('batch')=='15_motor_court_motel','new_assets_24':len(src.objects)==24,'all_images_packed':all(im.packed_file for im in bpy.data.images if im.name not in ['Render Result','Viewer Node']),'units':bpy.context.scene.unit_settings.scale_length==1,'disabled_physics':all(not o.rigid_body for o in ass.all_objects)}
if not all(native.values()):errors.append('Native file check failed')
vv=[];ff=[]
for ob in ass.all_objects:
 if ob.type!='MESH' or ob.get('presentation_only') or any(n in ob.name for n in ['room_floor','gallery_walkway','gallery_access_ramp']):continue
 start=len(vv);vv.extend(ob.matrix_world@v.co for v in ob.data.vertices);ob.data.calc_loop_triangles();ff.extend(tuple(start+i for i in t.vertices) for t in ob.data.loop_triangles)
bvh=BVHTree.FromPolygons(vv,ff,all_triangles=True)
paths={'west_guest_conveniences':[(-14.0,-.6,-.08),(-14.0,3.3,-.08)],'exterior_gallery':[(-11.2,-.85,0),(6.8,-.85,0)],'reception_to_laundry':[(-11.25,-.85,0),(-11.25,4.72,0),(-10,4.72,0),(-10,8.6,0)],'service_to_sidecourt':[(-10,8,0),(-6.8,8,0)],'court_ramp':[(-10.9,-3.01,-.078),(-10.9,-1.83,0),(-10.9,-.8,0)]}
for i,cx in enumerate([-6,-2,2,6]):paths['guest_room_'+str(101+i)+'_to_bathroom']=[(cx-1.2,-.85,0),(cx-1.2,.7,0),(cx-.15,1.12,0),(cx-.15,3.52,0),(cx-1.2,3.52,0),(cx-1.2,4.70,0)]
rr=[]
for name,path in paths.items():
 pts=[]
 for a,b in zip(path,path[1:]):
  a=Vector(a);b=Vector(b);n=max(1,math.ceil((b-a).length/.08));pts.extend(a+(b-a)*i/n for i in range(n))
 pts.append(Vector(path[-1]));violations=[];minimum=999
 for p in pts:
  for i in range(11):
   q=p+Vector((0,0,.30+i*.12));hit=bvh.find_nearest(q);d=hit[3];minimum=min(minimum,d)
   if d<.295:violations.append({'point_source_m':[round(float(v),3) for v in q],'distance_m':round(d,4)})
 ok=not violations
 if not ok:errors.append(name+' route failed')
 rr.append({'name':name,'status':'pass' if ok else 'fail','floor_polyline_m':path,'capsule_radius_m':.3,'capsule_height_m':1.8,'samples':len(pts)*11,'minimum_clearance_to_sphere_center_m':round(minimum,5),'violations':violations[:12]})
report={'status':'pass' if not errors else 'fail','errors':errors,'new_asset_reimports':rows,'example_reimports':examples,'native_scene_checks':native,'routes':rr,'scope':'Fresh imports and actual triangle proximity at 8cm route samples and 12cm vertical capsule samples. Only listed routes are checked. Not collider, navmesh, building code or game-engine QA.'}
json.dump(report,open(os.path.join(ROOT,'qa','reimport_route_report.json'),'w'),indent=2);print('MOTEL_QA',report['status'],errors)
if errors:raise RuntimeError('; '.join(errors))
