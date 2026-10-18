import bpy,sys,json,math
from pathlib import Path
from mathutils.bvhtree import BVHTree
out=Path(sys.argv[sys.argv.index('--')+1]);deps=bpy.context.evaluated_depsgraph_get();r=[]
shell=bpy.data.objects['Coachwork_continuous_arch_cut_shell']
for tire in [o for o in bpy.data.objects if o.name.endswith('_tire')]:
 tag='front' if 'front'in tire.name else 'rear';side='1' if '_left_'in tire.name else '-1';lip=bpy.data.objects[tag+'_arch_lip_'+side];tv=BVHTree.FromObject(tire,deps)
 for target in [shell,lip]:
  bv=BVHTree.FromObject(target,deps);overlap=tv.overlap(bv);ds1=[bv.find_nearest(tire.matrix_world@v.co)[3]for v in tire.data.vertices];ds2=[tv.find_nearest(target.matrix_world@v.co)[3]for v in target.data.vertices];r.append({'tire':tire.name,'target':target.name,'BVHTree_overlap_triangle_pairs':len(overlap),'overlap_pairs':overlap,'minimum_sampled_vertex_to_triangle_distance_m':min(ds1+ds2),'method':'Blender BVHTree overlap query and bidirectional all-vertex-to-triangle distance samples; no engine collision or steering envelope tested.'})
out.write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
