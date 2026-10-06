import bpy,json,hashlib
import numpy as np
from pathlib import Path
R=Path('/workspace/scratch/eac4961518d4/animation_rebuild');O=R/'grip_review_oct5/candidate_c1_review';base=None;out={}
def digest(x):return hashlib.sha256(json.dumps(x,separators=(',',':')).encode()).hexdigest()
def positions(o):e=o.evaluated_get(bpy.context.evaluated_depsgraph_get());m=e.to_mesh();V=np.array([e.matrix_world@v.co for v in m.vertices]);e.to_mesh_clear();return V
from gate_config import INPUTS,verify_loaded
for label,rel,expected_sha in INPUTS:
 p=R/rel;pin=hashlib.sha256(p.read_bytes()).hexdigest();assert pin==expected_sha;bpy.ops.wm.open_mainfile(filepath=str(p));verify_loaded(bpy,label);bpy.context.scene.frame_set(0);bpy.context.view_layer.update();r=bpy.data.objects['SWAT_Mixamo_Rig'];row={'source_sha256':pin,'saved_action':r.animation_data.action.name,'geometry':{},'rig_rest_hash':digest([(x.name,x.parent.name if x.parent else None,[list(q) for q in x.matrix_local],x.length) for x in r.data.bones]),'supplemental_left_elbow_axis_angle_degrees':float(np.degrees((r.pose.bones['mixamorig:LeftForeArm'].head-r.pose.bones['mixamorig:LeftArm'].head).angle(r.pose.bones['mixamorig:LeftHand'].head-r.pose.bones['mixamorig:LeftForeArm'].head)))}
 for name in ['SWAT_Wearer','Rifle 7','SWAT_ElbowCap_L','SWAT_ElbowCap_R','SWAT_Headset_L','SWAT_Headset_R','Removed magazine','Fresh magazine']:
  o=bpy.data.objects[name];row['geometry'][name]=digest({'vertices':[list(v.co) for v in o.data.vertices],'polygons':[list(t.vertices) for t in o.data.polygons],'weights':[{o.vertex_groups[g.group].name:g.weight for g in v.groups} for v in o.data.vertices],'uvs':{u.name:[list(x.uv) for x in u.data] for u in o.data.uv_layers}})
 G=positions(bpy.data.objects['Rifle 7'])
 if base is None:base=(row,G)
 else:row['comparison_to_A']={'all_listed_mesh_rest_topology_weight_uv_hashes_equal':row['geometry']==base[0]['geometry'],'rig_rest_hash_equal':row['rig_rest_hash']==base[0]['rig_rest_hash'],'maximum_evaluated_rifle_vertex_delta_mm':float(np.linalg.norm(G-base[1],axis=1).max()*1000)}
 assert hashlib.sha256(p.read_bytes()).hexdigest()==pin;row['source_sha256_after']=pin;out[label]=row
(O/'invariants_comparison.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out['C1'],indent=2),flush=True)
