"""Independent Blender fresh import, native mesh, assembly, support and route audit.
Run: OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 blender -b -t 2 --python-exit-code 1 --python qa/independent_blender_audit.py
Writes only qa/independent*; does not save or change source assets.
"""
import os
os.environ['OMP_NUM_THREADS']='2';os.environ['OPENBLAS_NUM_THREADS']='2'
import bpy,json,math,hashlib
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
from mathutils.geometry import closest_point_on_tri,intersect_ray_tri
ROOT=Path(__file__).resolve().parents[1];m=json.loads((ROOT/'manifest.json').read_text());a=json.loads((ROOT/'assembly_manifest.json').read_text());errors=[];checks=[]
input_files=['manifest.json','assembly_manifest.json','neighborhood_storefronts.blend',m['example']['file'],m['example']['cutaway']]
input_hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in input_files}
def ck(test,okay,**data):
 checks.append({'test':test,'status':'pass' if okay else 'fail',**data})
 if not okay:errors.append(test);print('AUDIT_FAILURE',test,json.dumps(data),flush=True)
def vlist(v):return [round(float(x),7) for x in v]
def points(ob):return [ob.matrix_world@v.co for v in ob.data.vertices]
def bounds(obs):
 pp=[v for ob in obs for v in points(ob)];return [[min(v[i] for v in pp) for i in range(3)],[max(v[i] for v in pp) for i in range(3)]]
def tris(obs):
 n=0
 for ob in obs:ob.data.calc_loop_triangles();n+=len(ob.data.loop_triangles)
 return n
def images_ok():return all(len(im.pixels)>0 and im.has_data and bool(im.packed_file) for im in bpy.data.images if im.name not in ['Render Result','Viewer Node'])
def expected(r):return Matrix.Translation(Vector(r['position_source_m']))@Matrix.Rotation(math.radians(r['yaw_source_deg']),4,'Z')@Matrix.Diagonal(Vector(r['scale']+[1]))
def delta(x,y):return max(abs(x[i][j]-y[i][j]) for i in range(4) for j in range(4))
fresh=[]
for kind,key in [('new','assets'),('reuse','reused_existing_assets')]:
 for r in m[key]:
  bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.scene.render.threads_mode='FIXED';bpy.context.scene.render.threads=2;bpy.ops.import_scene.gltf(filepath=str(ROOT/r['file']));bpy.context.view_layer.update();obs=[ob for ob in bpy.context.scene.objects if ob.type=='MESH'];bb=bounds(obs);tc=tris(obs);be=max(abs(bb[k][i]-r['bounds_m_source_xyz'][k][i]) for k in [0,1] for i in range(3));cs={'bounds':be<1e-4,'triangles':tc==r['triangles'],'images':images_ok(),'disabled_rigid_bodies':all(not ob.rigid_body for ob in obs)}
  if kind=='new':cs.update({'render_only':all(ob.get('render_only') is True and ob.get('collider_enabled') is False for ob in obs)})
  ck('fresh_import/'+r['id'],all(cs.values()),checks=cs,maximum_bounds_error_m=be,triangles=tc);fresh.append({'id':r['id'],'kind':kind,'checks':cs,'triangles':tc,'bounds_m_source_xyz':bb,'rotation_modes':[ob.rotation_mode for ob in obs]})
examples=[]
for file in [m['example']['file'],m['example']['cutaway']]:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(ROOT/file));bpy.context.view_layer.update();obs=[ob for ob in bpy.context.scene.objects if ob.type=='MESH'];cs={'scene_provenance':bpy.context.scene.get('batch')==m['batch'],'images':images_ok(),'render_only':all(ob.get('render_only') is True and ob.get('collider_enabled') is False and not ob.rigid_body for ob in obs)};ck('example_import/'+file,all(cs.values()),checks=cs,mesh_instances=len(obs));trs=[]
 for r in a['instances']:
  if 'cutaway' in file and 'ROOFS' in r['collection']:continue
  ob=bpy.data.objects.get(r['object']);err=delta(ob.matrix_world,expected(r)) if ob else 999;trs.append({'object':r['object'],'maximum_matrix_error':err});ck('example_transform/'+file+'/'+r['object'],err<2e-5,maximum_matrix_error=err)
 examples.append({'file':file,'checks':cs,'mesh_instances':len(obs),'transforms':trs})
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'neighborhood_storefronts.blend'));bpy.context.scene.render.threads_mode='FIXED';bpy.context.scene.render.threads=2
for name in ['01_NEW_ASSET_LIBRARY','01b_REUSED_LIBRARY']:bpy.data.collections[name].hide_viewport=False
bpy.context.view_layer.update();src=bpy.data.collections['01_NEW_ASSET_LIBRARY'];reuse=bpy.data.collections['01b_REUSED_LIBRARY'];ass=bpy.data.collections['02_STOREFRONTS_EXAMPLE'];obs=[ob for ob in ass.all_objects if ob.type=='MESH'];new={r['id']:r for r in m['assets']};native={'scene_provenance':bpy.context.scene.get('batch')==m['batch'],'new_asset_count':len(src.objects)==m['asset_count']==24,'reused_asset_count':len(reuse.objects)==len(m['reused_existing_assets']),'packed_images':images_ok(),'metre_units':bpy.context.scene.unit_settings.scale_length==1,'no_rigid_bodies':all(not ob.rigid_body for ob in obs),'render_only':all(ob.get('render_only') is True and ob.get('collider_enabled') is False for ob in obs)};ck('native_scene',all(native.values()),checks=native)
for r in a['instances']:
 ob=bpy.data.objects.get(r['object']);err=delta(ob.matrix_world,expected(r)) if ob else 999;recorderr=delta(ob.matrix_world,Matrix(r['matrix_world_source_row_major'])) if ob else 999;mode=bool(ob and ob.rotation_mode=='XYZ' and r['instance_rotation_mode']=='XYZ');ck('native_transform/'+r['object'],err<1e-5 and recorderr<1e-6 and mode,maximum_matrix_error=err,record_matrix_error=recorderr,rotation_mode=ob.rotation_mode if ob else None,source_rotation_mode=r.get('source_rotation_mode'),position=r['position_source_m'])
 if r['reused_existing_design']:
  source=next((ob for ob in reuse.objects if ob.get('asset_id')==r['asset_id']),None);ck('reuse_rotation_mode/'+r['object'],bool(source and source.rotation_mode==r['source_rotation_mode'] and mode),source_rotation_mode=source.rotation_mode if source else None,instance_rotation_mode=ob.rotation_mode if ob else None)
for r in m['assets']:
 for s in r['connections']:
  p=s['position_source_m'];n=s['outward_source'];ck('socket_basis/'+r['id']+'/'+s['name'],s['position_gltf_m']==[p[0],p[2],-p[1]] and s['outward_gltf']==[n[0],n[2],-n[1]])
def anchor(ob,name):return ob.matrix_world@Vector(next(c['position_source_m'] for c in new[ob['asset_id']]['connections'] if c['name']==name))
connections=[]
for con in a['connections']:
 ob1=bpy.data.objects[con['a']];ob2=bpy.data.objects[con['b']];p=anchor(ob1,con['socket_a']);q=anchor(ob2,con['socket_b']);err=(p-q).length;row={**con,'a_world_m':vlist(p),'b_world_m':vlist(q),'error_m':err};connections.append(row);ck('socket_connection/'+con['a']+'/'+con['socket_a'],err<1e-5,**row)
# Metric UV projection and PBR node contract on native new modules.
uvrows=[];tiles=m['material_uv_contract']['tiles_m'];uvmax=0;uvcount=0
for ob in src.objects:
 uv=ob.data.uv_layers.active;bad=0;maxerr=0
 for p in ob.data.polygons:
  mat=ob.data.materials[p.material_index];key=mat.name.removeprefix('NS16_');tile=tiles.get(key,[1,1]);axis=max(range(3),key=lambda i:abs(p.normal[i]));axes=[i for i in range(3) if i!=axis]
  for li in p.loop_indices:
   v=ob.data.vertices[ob.data.loops[li].vertex_index].co;ex=Vector((v[axes[0]]/tile[0],v[axes[1]]/tile[1]));err=(uv.data[li].uv-ex).length;maxerr=max(maxerr,err);bad+=err>2e-5;uvcount+=1
 uvmax=max(uvmax,maxerr);uvrows.append({'object':ob.name,'maximum_uv_error':maxerr,'bad_uv_corners':bad});ck('native_metric_uv/'+ob.name,bad==0,maximum_uv_error=maxerr,bad_uv_corners=bad)
floor_source=next(ob for ob in src.objects if ob.get('asset_id')=='ns16_floor_slab_2m')
slab_group=floor_source.vertex_groups['slab'].index;finish_group=floor_source.vertex_groups['floor_finish'].index
slab_top=max(v.co.z for v in floor_source.data.vertices if any(g.group==slab_group for g in v.groups));finish_top=max(v.co.z for v in floor_source.data.vertices if any(g.group==finish_group for g in v.groups))
ck('floor_finish_not_coplanar',finish_top-slab_top>.007 and abs(finish_top)<1e-6,slab_top_z_m=slab_top,finish_top_z_m=finish_top)
for mat in [mat for mat in bpy.data.materials if mat.name.startswith('NS16_')]:
 if not mat.use_nodes:continue
 bs=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
 if mat.name=='NS16_glass':ck('native_glass_alpha',bool(bs and abs(bs.inputs['Alpha'].default_value-.2)<1e-6),alpha=bs.inputs['Alpha'].default_value if bs else None)
 for n in mat.node_tree.nodes:
  if n.type=='NORMAL_MAP':ck('native_normal_map/'+mat.name,n.space=='TANGENT' and abs(n.inputs['Strength'].default_value-1)<1e-6,space=n.space,strength=n.inputs['Strength'].default_value)
  if n.type=='TEX_IMAGE' and n.image and n.image.name.endswith('_basecolor.png'):ck('srgb_basecolor/'+mat.name+'/'+n.image.name,n.image.colorspace_settings.name=='sRGB',color_space=n.image.colorspace_settings.name)
  if n.type=='TEX_IMAGE' and n.image and any(n.image.name.endswith('_'+ch+'.png') for ch in ['normal','roughness']):ck('linear_texture/'+mat.name+'/'+n.image.name,n.image.colorspace_settings.name=='Non-Color',color_space=n.image.colorspace_settings.name)
def bvh_for(objects):
 vv=[];ff=[];owners=[]
 for ob in objects:
  offset=len(vv);vv.extend(points(ob));ob.data.calc_loop_triangles()
  for t in ob.data.loop_triangles:ff.append(tuple(offset+i for i in t.vertices));owners.append(ob.name)
 return BVHTree.FromPolygons(vv,ff,all_triangles=True),owners
supportrows=[]
for spec in a.get('supported_props',[]):
 ob=bpy.data.objects[spec['object']];surface=bpy.data.objects[spec['support']];pp=points(ob);lo=min(v.z for v in pp);under=[p for p in pp if p.z<=lo+.002];bvh,_=bvh_for([surface]);rays=[];seen=set()
 for p in under+[Vector((ob.location.x,ob.location.y,lo))]:
  key=tuple(round(float(v),5) for v in p[:2])
  if key in seen:continue
  seen.add(key);hit=bvh.ray_cast(Vector((p.x,p.y,lo+.025)),Vector((0,0,-1)),.25);gap=lo-hit[0].z if hit[0] else None;rays.append({'xy_m':[p.x,p.y],'lowest_z_m':lo,'support_z_m':hit[0].z if hit[0] else None,'gap_m':gap})
 okay=any(r['gap_m'] is not None and -.003<=r['gap_m']<=spec.get('tolerance_m',.012) for r in rays);row={**spec,'status':'pass' if okay else 'fail','prop_position_m':vlist(ob.location),'support_position_m':vlist(surface.location),'rays':rays};supportrows.append(row);ck('triangle_support/'+ob.name,okay,**row)
# Exact capsule axis-segment to triangle distance at 50 mm horizontal route samples.
# BVH candidate acquisition samples the axis every 25 mm at a conservative 0.313 m radius,
# then tests full axis-segment distance. Actual floors remain in the triangle set.
all_bvh,owners=bvh_for(obs);triangle_vertices=[]
for ob in obs:
 pp=points(ob);ob.data.calc_loop_triangles();triangle_vertices.extend(tuple(pp[i] for i in t.vertices) for t in ob.data.loop_triangles)
floorobs=[o for o in obs if o.get('asset_id')=='ns16_floor_slab_2m' or o.name in ['SITE_sidewalk','SITE_rear_apron']];floor_bvh,floorowners=bvh_for(floorobs)
def segment_segment_distance(p1,q1,p2,q2):
 d1=q1-p1;d2=q2-p2;r=p1-p2;a1=d1.dot(d1);e=d2.dot(d2);f=d2.dot(r);eps=1e-16
 if a1<=eps and e<=eps:return (p1-p2).length
 if a1<=eps:s=0;t=max(0,min(1,f/e))
 else:
  c=d1.dot(r)
  if e<=eps:t=0;s=max(0,min(1,-c/a1))
  else:
   b=d1.dot(d2);den=a1*e-b*b;s=max(0,min(1,(b*f-c*e)/den)) if den>eps else 0;t=(b*s+f)/e
   if t<0:t=0;s=max(0,min(1,-c/a1))
   elif t>1:t=1;s=max(0,min(1,(b-c)/a1))
 return ((p1+d1*s)-(p2+d2*t)).length

def segment_triangle_distance(p,q,tri):
 a1,b,c=tri;direction=q-p;inter=intersect_ray_tri(a1,b,c,direction,p,True)
 if inter is not None and -.0000001<=(inter-p).dot(direction)<=direction.length_squared+.0000001:return 0
 ds=[(p-closest_point_on_tri(p,a1,b,c)).length,(q-closest_point_on_tri(q,a1,b,c)).length]
 ds.extend(segment_segment_distance(p,q,u,v) for u,v in [(a1,b),(b,c),(c,a1)])
 return min(ds)
paths={'laundromat_front_to_shared_rear':[(-3,-1,0),(-3,1.4,0),(-3,4.6,0),(-3,6.8,0),(-3,8,0)],'pawn_repair_front_to_shared_rear':[(3,-1,0),(3,1.4,0),(3,4.6,0),(3,6.8,0),(3,8,0)],'shared_rear_cross_passage':[(3,8,0),(-3,8,0)],'shared_rear_exit':[(-3,8,0),(-3,9.5,0),(-3,11.3,0)],'front_sidewalk_between_entries':[(-3,-1,0),(3,-1,0)]}
routes=[]
for name,path in paths.items():
 ps=[]
 for p,q in zip(path,path[1:]):
  p=Vector(p);q=Vector(q);nn=max(1,math.ceil((q-p).length/.05));ps.extend(p+(q-p)*i/nn for i in range(nn))
 ps.append(Vector(path[-1]));violations=[];floorfails=[];mind=999;tested=0;allowed_steps=0;nearest=None;collisions=0
 for p in ps:
  hit=floor_bvh.ray_cast(p+Vector((0,0,.15)),Vector((0,0,-1)),.30)
  if hit[0] is None or abs(hit[0].z-p.z)>.025 or abs(hit[1].z)<.9:floorfails.append({'point_m':vlist(p),'floor_hit_m':vlist(hit[0]) if hit[0] else None})
  axis0=p+Vector((0,0,.3));axis1=p+Vector((0,0,1.5));candidates=set()
  for i in range(49):
   q=axis0+Vector((0,0,i*.025))
   for hit in all_bvh.find_nearest_range(q,.313):candidates.add(hit[2])
  for index in candidates:
   tri=triangle_vertices[index];d=segment_triangle_distance(axis0,axis1,tri);tested+=1;owner=owners[index]
   if d<mind:mind=d;nearest=owner
   if d<.297:
    # Only explicitly documented ≤20 mm doorway sill / sidewalk joints are allowed steps.
    isstep=(('entry_frame' in owner or 'sidewalk_joint' in owner) and max(v.z for v in tri)<=.0201)
    if isstep:allowed_steps+=1;continue
    collisions+=1
    if len(violations)<30:violations.append({'capsule_floor_point_m':vlist(p),'triangle_owner':owner,'triangle_vertices_m':[vlist(v) for v in tri],'axis_segment_triangle_distance_m':d})
 row={'name':name,'status':'pass' if not collisions and not floorfails else 'fail','floor_polyline_m':path,'capsule_radius_m':.3,'capsule_height_m':1.8,'horizontal_sample_spacing_max_m':.05,'horizontal_route_samples':len(ps),'distance_method':'Exact axis-segment to triangle closest distance, with conservative BVH candidates','exact_triangle_distance_tests':tested,'minimum_triangle_distance_m':mind,'nearest_owner':nearest,'allowed_threshold_step_triangle_count':allowed_steps,'maximum_allowed_step_m':.0201,'collision_count':collisions,'violations':violations,'floor_support_failures':floorfails[:30]};routes.append(row);ck('route/'+name,not collisions and not floorfails,**row)
ck('inputs_stable_during_audit',all(hashlib.sha256((ROOT/f).read_bytes()).hexdigest()==h for f,h in input_hashes.items()))
report={'status':'pass' if not errors else 'fail','audited_input_sha256':input_hashes,'scope':'CPU 2-thread fresh GLB imports; native scene and exact assembly transforms; local-to-world sockets; actual triangle support rays; native metric UV and PBR contract; 0.30 m radius / 1.80 m height capsule routes using exact capsule-axis segment to actual triangle distances at 50 mm horizontal samples, 3 mm numeric tolerance and explicitly allowed ≤20 mm doorway threshold steps. Floor support is actual triangle ray testing. Listed routes only; no collider/navmesh/runtime or building-code certification.','errors':errors,'fresh_imports':fresh,'example_imports':examples,'native_scene_checks':native,'connections':connections,'uv_projection':{'corners_checked':uvcount,'maximum_error':uvmax,'assets':uvrows},'supported_props':supportrows,'routes':routes,'checks':checks};(ROOT/'qa/independent_blender_report.json').write_text(json.dumps(report,indent=2));print('INDEPENDENT_BLENDER_AUDIT',report['status'],errors,flush=True)
if errors:raise RuntimeError('; '.join(errors))
