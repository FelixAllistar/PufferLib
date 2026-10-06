import bpy,os,json,math,hashlib
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'));bpy.context.view_layer.update()
m=json.load(open(os.path.join(ROOT,'manifest.json')));a=json.load(open(os.path.join(ROOT,'assembly_manifest.json')));obs=list(bpy.data.collections['02_MOTEL_EXAMPLE'].all_objects);new={r['id']:r for r in m['assets']};errs=[];checks=[]
def check(name,okay,data):
 checks.append({'test':name,'status':'pass' if okay else 'fail',**data})
 if not okay:errs.append(name)
def select(aid):return [o for o in obs if o.get('asset_id')==aid]
def anchor(ob,name):
 p=next(v['position_source_m'] for v in new[ob['asset_id']]['connections'] if v['name']==name);return ob.matrix_world@Vector(p)
def meshbvh(ob):
 ob.data.calc_loop_triangles();return BVHTree.FromPolygons([ob.matrix_world@v.co for v in ob.data.vertices],[t.vertices[:] for t in ob.data.loop_triangles],all_triangles=True)
for rec in m['assets']:
 for c in rec['connections']:
  p=c['position_source_m'];n=c['outward_source'];check(rec['id']+'/'+c['name']+'/basis',c['position_gltf_m']==[p[0],p[2],-p[1]] and c['outward_gltf']==[n[0],n[2],-n[1]],{})
for rec in a['instances']:
 ob=bpy.data.objects[rec['object']];expected=Matrix.Translation(Vector(rec['position_source_m']))@Matrix.Rotation(math.radians(rec['yaw_source_deg']),4,'Z')@Matrix.Diagonal(Vector(rec['scale']+[1]));delta=max(abs(ob.matrix_world[i][j]-expected[i][j]) for i in range(4) for j in range(4));check('instance_transform_'+ob.name,delta<1e-5,{'maximum_matrix_error':delta})
facades=sorted(select('mc15_room_facade_4m'),key=lambda o:o.location.x)
doors=select('mc15_room_door_leaf');windows=select('mc15_room_window_insert')
for f in facades:
 d=min(doors,key=lambda o:(o.location-anchor(f,'door_hinge')).length);w=min(windows,key=lambda o:(o.location-anchor(f,'window')).length)
 for key,ob in [('door_hinge',d),('window',w)]:
  error=(ob.location-anchor(f,key)).length;check(f.name+'/'+key,error<1e-5,{'error_m':error})
for left,right in zip(facades,facades[1:]):
 delta=(anchor(left,'right')-anchor(right,'left')).length;check('adjacent_facades_'+left.name,delta<1e-5,{'error_m':delta})
for p in select('mc15_canopy_post'):
 head=anchor(p,'head');check('post_canopy_'+p.name,abs(head.z-2.44)<1e-5,{'head_world_z_m':head.z,'canopy_bottom_z_m':2.44})
for im in m['reused_existing_assets']:
 data=open(os.path.join(ROOT,im['file']),'rb').read();check('reuse_hash_'+im['id'],hashlib.sha256(data).hexdigest()==im['sha256'],{'sha256':im['sha256']})
# Actual ray-to-triangle support heights, not only bounding box assumptions.
for bed in select('bedframe_single_institutional'):
 mat=min(select('mattress_dirty'),key=lambda o:(o.location-bed.location).length);bvh=meshbvh(bed);top=bvh.ray_cast(Vector((bed.location.x,bed.location.y,.6)),Vector((0,0,-1)),1)[0];bottom=min((mat.matrix_world@v.co).z for v in mat.data.vertices);gap=bottom-top.z if top else 999
 check('mattress_support_'+bed.name,-.005<=gap<=.015,{'gap_m':gap,'frame_contact_height_m':top.z if top else None,'mattress_lowest_z_m':bottom})
 sheet=min(select('lb11_folded_bedsheet_stack'),key=lambda o:(o.location-mat.location).length);hit=meshbvh(mat).ray_cast(Vector((sheet.location.x,sheet.location.y,1.4)),Vector((0,0,-1)),2)[0];low=min((sheet.matrix_world@v.co).z for v in sheet.data.vertices);gap=low-hit.z if hit else 999
 check('sheets_support_'+bed.name,-.006<=gap<=.02,{'gap_m':gap,'surface_height_m':hit.z if hit else None,'sheets_lowest_z_m':low})
for aid in ['desk','bedframe_single_institutional','mattress_dirty','bathtub_old','toilet_old','basin_pedestal']:
 for ob in select(aid):
  if ob.location.y>6:continue
  cx=min([-6,-2,2,6],key=lambda x:abs(x-ob.location.x))
  if ob.location.x < -8:continue
  pts=[ob.matrix_world@v.co for v in ob.data.vertices];lo=min(p.x for p in pts);hi=max(p.x for p in pts)
  check('room_containment_'+ob.name,lo>=cx-1.91-.002 and hi<=cx+1.91+.002,{'room_center_x':cx,'bounds_world_x_m':[lo,hi],'interior_x_m':[cx-1.91,cx+1.91]})
# Master 101 design is instanced with explicit label variants, not four counted pieces.
nums=[o.get('number_variant') for o in select('mc15_room_number_plaque')];check('room_numbers',sorted(nums)==[101,102,103,104],{'numbers':sorted(nums)})
report={'status':'pass' if not errs else 'fail','errors':errs,'checks':checks,'scope':'World-space socket/hinge alignment, repeated bays, canopy post heights, source hashes, actual vertical furnishing support rays and room-number variants. Static geometry only.'};json.dump(report,open(os.path.join(ROOT,'qa','assembly_connection_report.json'),'w'),indent=2);print(report['status'],errs)
if errs:raise RuntimeError('; '.join(errs))
