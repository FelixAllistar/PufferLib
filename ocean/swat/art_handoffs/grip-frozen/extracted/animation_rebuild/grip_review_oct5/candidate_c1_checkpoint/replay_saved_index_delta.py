"""Rebuild exact C1 from frozen C plus the saved four Index1 channel values.
Run from any extracted checkpoint: blender -b --python path/to/this.py.
No optimization, network, original-file writes, or engine changes are performed.
"""
import bpy,json,hashlib,sys
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
AUTHOR=ROOT/'animation_rebuild/grip_review_oct5/candidate_c1/CANDIDATE_C1_AUTHORING.json' if (ROOT/'animation_rebuild').exists() else HERE.parents[1]/'candidate_c/CANDIDATE_C_AUTHORING.json'
# Locate the preserved project tree, both in this workspace and in portable extraction.
for ancestor in [HERE,*HERE.parents]:
    if (ancestor/'animation_rebuild/upper_body_gear_rebuild/stage_f/upper_gear_stage_f.editable.blend').is_file():
        ROOT=ancestor;break
else: raise RuntimeError('Preserved animation_rebuild tree not found beside checkpoint')
R=ROOT/'animation_rebuild'; report=json.loads((R/'grip_review_oct5/candidate_c1/CANDIDATE_C1_AUTHORING.json').read_text())
BASE=R/'grip_review_oct5/candidate_c/support_grip_surface_c.editable.blend'
CAND=R/'grip_review_oct5/candidate_c1/support_grip_index_release_c1.editable.blend'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(BASE)==report['source_C_sha256'];assert sha(CAND)==report['output_sha256']
def snapshot():
    rig=bpy.data.objects['SWAT_Mixamo_Rig'];a=rig.animation_data.action;curves={}
    for f in a.fcurves:
        curves[f.data_path+'|'+str(f.array_index)]=[[list(k.co),k.interpolation,list(k.handle_left),list(k.handle_right),k.handle_left_type,k.handle_right_type]for k in f.keyframe_points]
    mats={b.name:np.array(b.matrix) for b in rig.pose.bones}
    verts={};dg=bpy.context.evaluated_depsgraph_get()
    for o in bpy.data.objects:
        if o.type!='MESH' or o.name=='Rebuild Review Floor':continue
        e=o.evaluated_get(dg);m=e.to_mesh();verts[o.name]=np.array([e.matrix_world@v.co for v in m.vertices]);e.to_mesh_clear()
    return curves,mats,verts
bpy.ops.wm.open_mainfile(filepath=str(CAND));bpy.context.scene.frame_set(0);bpy.context.view_layer.update();ref=snapshot()
bpy.ops.wm.open_mainfile(filepath=str(BASE));r=bpy.data.objects['SWAT_Mixamo_Rig'];a=bpy.data.actions['Support Grip / Surface Constrained Candidate C'].copy();a.name=report['action'];a.use_fake_user=True
for row in report['changed_curves']:
    f=a.fcurves.find(row['data_path'],index=row['index'])
    if f is None:f=a.fcurves.new(row['data_path'],index=row['index'],action_group='mixamorig:LeftHandIndex1')
    for k in list(f.keyframe_points):f.keyframe_points.remove(k)
    f.keyframe_points.insert(0,row['C1_key_values'][0][1]).interpolation='LINEAR'
r.animation_data.action=a;bpy.context.scene.frame_set(0);bpy.context.view_layer.update();got=snapshot()
assert got[0]==ref[0], 'Serialized F-curves differ from sealed candidate'
pose_error=max(float(np.max(np.abs(got[1][n]-v)))for n,v in ref[1].items())
vertex_error=max(float(np.max(np.linalg.norm(got[2][n]-v,axis=1)))for n,v in ref[2].items())
assert pose_error<1e-7 and vertex_error<1e-7
out=HERE/'replay_output';out.mkdir(exist_ok=True);bpy.context.preferences.filepaths.save_version=0;bpy.ops.wm.save_as_mainfile(filepath=str(out/'support_grip_c1.replayed.blend'),compress=True)
receipt={'status':'Exact saved scalar-curve replay passes; generated Blender file is not claimed byte-identical','candidate_sha256':sha(CAND),'baseline_sha256':sha(BASE),'action':a.name,'curves':len(got[0]),'changed_curve_channels':len(report['changed_curves']),'max_pose_matrix_element_error':pose_error,'max_evaluated_vertex_error_m':vertex_error}
(out/'REPLAY_RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
assert sha(BASE)==report['source_C_sha256'] and sha(CAND)==report['output_sha256']
