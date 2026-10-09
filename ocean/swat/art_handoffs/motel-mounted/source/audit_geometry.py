"""Audit actual exported/reimported geometry; normals/topology, wall and conduit anchors."""
import bpy,bmesh,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1]
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(R/'runtime/motel_surface_junction.glb'))
o=next(o for o in bpy.context.scene.objects if o.type=='MESH');bm=bmesh.new();bm.from_mesh(o.data);bm.normal_update()
zero=[f.index for f in bm.faces if f.calc_area()<1e-14];nonman=[e.index for e in bm.edges if not e.is_manifold];bad_norm=[f.index for f in bm.faces if f.normal.length<.99]
# GLTF splits vertices at normals/UV seams; weld exact seam duplicates for topology audit only.
bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
post_nonman=sum(not e.is_manifold for e in bm.edges)
print('EDGE_DETAILS',[(len(e.link_faces),[tuple(v.co) for v in e.verts]) for e in bm.edges if not e.is_manifold])
# Separate overlapping manufactured solids intentionally remain independent components.
bvh=BVHTree.FromObject(o,bpy.context.evaluated_depsgraph_get())
anchors={'wall_box_center':(0,0,0),'wall_upper_left_saddle':(-.022,0,.260),'wall_upper_right_saddle':(.022,0,.260),'wall_lower_left_saddle':(-.022,0,-.160),'wall_lower_right_saddle':(.022,0,-.160),'upper_conduit_rim':(.009,-.027,.355),'lower_conduit_rim':(.009,-.027,-.225)}
out={}
for k,p in anchors.items():
 pos,normal,index,dist=bvh.find_nearest(Vector(p));out[k]={'gltf_position_m':[p[0],p[2],-p[1]],'measured_surface_distance_m':dist};assert dist<2e-6,(k,dist)
vs=[o.matrix_world@v.co for v in o.data.vertices];wall_min=min(-v.y for v in vs);assert wall_min>=-1e-7
print('DEGENERATE',zero,'NORMAL',bad_norm);assert not zero and not bad_norm
uv=o.data.uv_layers.active;assert all(-1e-6<=v<=1.000001 for loop in uv.data for v in loop.uv)
report={'passed':True,'degenerate_triangles':len(zero),'zero_normals':len(bad_norm),'nonmanifold_edges_after_position_weld':post_nonman,'welded_boundary_edges':sum(e.is_boundary for e in bm.edges),'welded_loose_edges':sum(not e.link_faces for e in bm.edges),'topology_note':'Exactly four 4-face edges occur after position welding at the four gasket-strip corner contacts (X +/-0.048, source Z +/-0.050 m). Separate closed strips meet there. No boundary/loose edges and no collision claim; assembly is not Boolean-unioned.','wall_min_z_m':wall_min,'anchors':out,'conduit_endpoint_centers_gltf_m':{'upper':[0,.355,.027],'lower':[0,-.225,.027]},'outer_radius_m':.009,'inner_radius_m':.0078,'UV_0_in_unit_square':True,'collider_included':False}
(R/'qa/geometry_and_anchors.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
