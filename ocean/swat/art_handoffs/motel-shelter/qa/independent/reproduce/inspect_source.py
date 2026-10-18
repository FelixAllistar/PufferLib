import bpy,bmesh,json,sys,hashlib,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
D=Path(__file__).parent
src=D/'packed_only/asset.blend'
bpy.ops.wm.open_mainfile(filepath=str(src))
objs={o.name:o for o in bpy.data.objects if o.type=='MESH'};dep=bpy.context.evaluated_depsgraph_get()
rows=[];verts={};bvhs={}
for name,o in objs.items():
 ev=o.evaluated_get(dep);me=ev.to_mesh();vs=[o.matrix_world@v.co for v in me.vertices];me.calc_loop_triangles();fs=[tuple(t.vertices) for t in me.loop_triangles]
 bm=bmesh.new();bm.from_mesh(me)
 rows.append({'name':name,'vertices':len(vs),'triangles':len(fs),'material':o.data.materials[0].name,'bounds_blender_xyz_m':[[min(p[i] for p in vs) for i in range(3)],[max(p[i] for p in vs) for i in range(3)]],'boundary_edges':sum(e.is_boundary for e in bm.edges),'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),'signed_volume_m3':bm.calc_volume(signed=True),'modifiers':[m.type for m in o.modifiers]})
 bm.free();bvhs[name]=BVHTree.FromPolygons(vs,fs,all_triangles=True,epsilon=0.0);verts[name]=vs;ev.to_mesh_clear()
bounds={r['name']:r['bounds_blender_xyz_m'] for r in rows}
def pair(a,b):
 overlap=len(bvhs[a].overlap(bvhs[b])); ds=[bvhs[b].find_nearest(v)[3] for v in verts[a]]+[bvhs[a].find_nearest(v)[3] for v in verts[b]]
 return {'a':a,'b':b,'intersecting_triangle_pairs':overlap,'min_vertex_to_surface_distance_m':min(d for d in ds if d is not None),'scope':'BVH triangle intersection or minimum vertex-to-surface distance; no structural certification. Minimum is not a full edge-edge separation proof.'}
pairs=[]
for side in ['Left','Right']:
 for end in ['Front','Rear']:
  n=side+'_'+end
  for a,b in [('Baseplate_'+n,'Post_'+n),('Post_'+n,'RoofSideRail_'+side),('Post_'+n,'RoofCrossRail_'+end),('KneeAcross_'+n,'Post_'+n),('KneeAcross_'+n,'RoofCrossRail_'+end)]:pairs.append(pair(a,b))
for tag in ['Left','Right']:
 pairs.append(pair('RearScreen_'+tag+'_FoldedOpaque','RearCenterMullion'))
 for rail in ['Lower','Middle','Upper']:pairs.append(pair('RearScreen_'+tag+'_FoldedOpaque','RearRail_'+rail))
for j in range(1,4):
 n=f'{j:02}'
 for k in range(1,5):pairs.append(pair('BenchSeatBearer_'+n,f'BenchSeatSlat_{k:02}'))
 for k in range(1,4):pairs.append(pair('BenchBackUpright_'+n,f'BenchBackSlat_{k:02}'))
 for a,b in [('BenchSeatBearer_'+n,'BenchRearSupportRail'),('BenchBrace_'+n,'BenchSeatBearer_'+n),('BenchBrace_'+n,'BenchBracketMount_'+n),('BenchBracketMount_'+n,'RearRail_Lower')]:pairs.append(pair(a,b))
for x in ['Left','Right']:pairs.append(pair('BenchRearSupportRail','Post_'+x+'_Rear'))
for r in ['RoofCrossRail_Front','RoofCrossRail_Rear','RoofSideRail_Left','RoofSideRail_Right','RoofPurlin_A','RoofPurlin_B']:
 pairs.append(pair('Roof_ClosedCorrugatedSheet',r))
# Ray through visible drain center. A floor hit would indicate a blocked gutter outlet.
origin=Vector((1.43,.77,2.24));direction=Vector((0,0,-1));hit=bvhs['Gutter_OpenTrough'].ray_cast(origin,direction,.2)
drain={'gutter_center_vertical_ray_origin_blender_xyz':list(origin),'direction':list(direction),'range_m':.2,'hit':list(hit[0]) if hit[0] else None,'gutter_bounds':bounds['Gutter_OpenTrough'],'outlet_bounds':bounds['Downpipe_HollowBentOutlet']}
# Coplanar bench supports require vertical rays; vertex-only distance is not reliable here.
seat_contacts=[]
for j,x in enumerate([-1.03,0,1.03],1):
 for k,y in enumerate([.02625,.13875,.25125,.36375],1):
  bearer=f'BenchSeatBearer_{j:02}';slat=f'BenchSeatSlat_{k:02}'
  a=bvhs[bearer].ray_cast(Vector((x+.0043,y+.0031,.44)),Vector((0,0,-1)),.2)[0]
  b=bvhs[slat].ray_cast(Vector((x+.0043,y+.0031,.41)),Vector((0,0,1)),.2)[0]
  seat_contacts.append({'bearer':bearer,'slat':slat,'blender_x_y_m':[x+.0043,y+.0031],'bearer_top_z_m':a.z if a else None,'slat_bottom_z_m':b.z if b else None,'vertical_gap_m':b.z-a.z if a and b else None})
clip_pairs=[]
for name in objs:
 if name.startswith('PanelClip_'):
  side='Left' if '_Left_' in name else 'Right';z=float(name.split('_')[-1]);rail='Lower' if z<.3 else 'Middle' if z<1.5 else 'Upper'
  clip_pairs.extend([pair(name,'RearScreen_'+side+'_FoldedOpaque'),pair(name,'RearRail_'+rail)])
endcap_pairs=[pair('Gutter_EndCap_'+side,'Gutter_OpenTrough') for side in ['Left','Right']]
images=[]
for im in bpy.data.images:
 if im.source=='FILE':images.append({'name':im.name,'filepath':im.filepath,'packed':bool(im.packed_file),'packed_sha256':hashlib.sha256(im.packed_file.data).hexdigest() if im.packed_file else None,'size':list(im.size),'colorspace':im.colorspace_settings.name,'sample_pixels':list(im.pixels[:4])})
r={'source':str(src),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'blender_version':bpy.app.version_string,'scene_unit_system':bpy.context.scene.unit_settings.system,'scene_scale_length':bpy.context.scene.unit_settings.scale_length,'maps_directory_absent':not (src.parent/'maps').exists(),'source_mesh_count':len(objs),'images':images,'parts':rows,'contact_pairs':pairs,'seat_contact_rays':seat_contacts,'screen_clip_pairs':clip_pairs,'gutter_endcap_pairs':endcap_pairs,'drain':drain,'anchors':[{'name':o.name,'location_blender_xyz_m':list(o.location),'parent':o.parent.name if o.parent else None,'properties':dict(o.items())} for o in bpy.data.objects if o.type=='EMPTY']}
(D/'source_audit.json').write_text(json.dumps(r,indent=2));print('SOURCE_AUDIT',len(objs),'parts',len(images),'images',len(pairs),'contact pairs')
for p in pairs:
 if not p['intersecting_triangle_pairs'] and p['min_vertex_to_surface_distance_m']>.001: print('CONTACT_REVIEW',json.dumps(p))
print('DRAIN',json.dumps(drain))
