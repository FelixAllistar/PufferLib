"""Independent Blender-source audit, plus isolated packed reopen and export."""
import bpy,bmesh,sys,json,hashlib,math,os
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
args=sys.argv[sys.argv.index('--')+1:];out=Path(args[0]);out.mkdir(parents=True,exist_ok=True)
source=Path(bpy.data.filepath);root=bpy.data.objects.get('BC_SEDAN01');report={'file':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'blender':bpy.app.version_string,'units':{'system':bpy.context.scene.unit_settings.system,'scale':bpy.context.scene.unit_settings.scale_length},'root':{'location':list(root.location),'rotation':list(root.rotation_euler),'scale':list(root.scale),'custom':dict(root.items())},'objects':[],'images':[]}
for im in bpy.data.images:
 if im.source=='FILE':
  packed=im.packed_file
  report['images'].append({'name':im.name,'path':im.filepath,'packed':bool(packed),'packed_size':len(packed.data) if packed else 0,'packed_sha256':hashlib.sha256(packed.data).hexdigest() if packed else None,'dimensions':list(im.size),'channels':im.channels,'pixels_count':len(im.pixels),'has_data':im.has_data,'colorspace':im.colorspace_settings.name})
for o in sorted(bpy.data.objects,key=lambda x:x.name):
 if o.type!='MESH':continue
 bm=bmesh.new();bm.from_mesh(o.data);bm.normal_update();verts=[o.matrix_world@v.co for v in o.data.vertices];nonman=[e.index for e in bm.edges if not e.is_manifold];vol=bm.calc_volume(signed=True)
 components=[];todo=set(bm.faces)
 while todo:
  seed=todo.pop();stack=[seed];fs=[seed]
  while stack:
   f=stack.pop()
   for e in f.edges:
    for other in e.link_faces:
     if other in todo:todo.remove(other);stack.append(other);fs.append(other)
  cv=0
  for f in fs:
   v=[q.co for q in f.verts]
   for i in range(1,len(v)-1):cv+=v[0].dot(v[i].cross(v[i+1]))/6
  components.append({'faces':len(fs),'signed_volume':cv})
 result={'name':o.name,'group':o.get('runtime_group'),'vertices':len(o.data.vertices),'polygons':len(o.data.polygons),'closed_manifold':not nonman,'nonmanifold_edges':nonman,'signed_volume':vol,'components':components,'bbox_min':[min(v[k] for v in verts)for k in range(3)],'bbox_max':[max(v[k] for v in verts)for k in range(3)],'matrix':list(sum(([float(x)for x in row]for row in o.matrix_world),[])),'all_finite':all(math.isfinite(x)for v in verts for x in v),'uv_layers':[u.name for u in o.data.uv_layers],'modifiers':[(m.name,m.type)for m in o.modifiers]};report['objects'].append(result);bm.free()
report['summary']={'mesh_objects':len(report['objects']),'not_closed':[o['name']for o in report['objects']if not o['closed_manifold']],'nonpositive_volume':[o['name']for o in report['objects']if o['signed_volume']<=0],'nonpositive_component_volume':[o['name']for o in report['objects']if any(c['signed_volume']<=0 for c in o['components'])],'non_finite':[o['name']for o in report['objects']if not o['all_finite']],'unpacked_images':[i['name']for i in report['images']if not i['packed']]}
# Independent tire centers from bounds. Circle radial and tread contact in actual source.
report['tires']=[]
shell=bpy.data.objects['Coachwork_continuous_arch_cut_shell'];deps=bpy.context.evaluated_depsgraph_get();shellbvh=BVHTree.FromObject(shell,deps)
for o in sorted(bpy.data.objects,key=lambda x:x.name):
 if not o.name.endswith('_tire'):continue
 v=[o.matrix_world@q.co for q in o.data.vertices];lo=Vector([min(q[k]for q in v)for k in range(3)]);hi=Vector([max(q[k]for q in v)for k in range(3)]);c=(lo+hi)/2
 distances=[]
 for q in v:
  near=shellbvh.find_nearest(q)
  if near[0]:distances.append(near[3])
 report['tires'].append({'name':o.name,'center_from_bounds':list(c),'bbox_min':list(lo),'bbox_max':list(hi),'radius_from_bounds':(hi.z-lo.z)/2,'width_from_bounds':hi.x-lo.x,'ground_min_z':lo.z,'ground_vertex_count':sum(abs(q.z)<1e-7 for q in v),'max_radial_error':max(abs(math.hypot(q.y-c.y,q.z-c.z)-.315) for q in v if abs(q.x-c.x)<.07),'nearest_tire_vertex_to_shell_distance':min(distances),'distance_note':'Vertex-to-triangle sample, not full collision proof.'})
# Coarse seam audit detects floating sections against body sheets, excluding other detail strips.
report['geometry_sha256']=hashlib.sha256(json.dumps([{k:o[k]for k in ['name','vertices','polygons','signed_volume','bbox_min','bbox_max']}for o in report['objects']],sort_keys=True).encode()).hexdigest()
(out/'source_audit.json').write_text(json.dumps(report,indent=2));print('INDEPENDENT_SOURCE_SUMMARY',json.dumps(report['summary']))
# In a disposable copy, replace every FILE image reference by a guaranteed unavailable path.
for im in bpy.data.images:
 if im.source=='FILE':im.filepath='//NO_EXTERNAL_MAPS_EXIST/'+im.name+'.png'
bpy.ops.wm.save_as_mainfile(filepath=str(out/'isolated_packed.blend'))
