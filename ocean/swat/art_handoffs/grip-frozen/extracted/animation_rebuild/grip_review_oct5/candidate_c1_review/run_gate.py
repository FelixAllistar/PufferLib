"""Orchestrate explicitly pinned read-only review. No candidate is chosen automatically."""
import sys,json,time,traceback
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from gate_config import INPUTS,OUTPUT,CANDIDATE,CANDIDATE_SHA,EXPECTED_ACTION,sha
stages=['gate_invariants.py','gate_skin_contacts.py','gate_palm_distribution.py','gate_skin_finger_axes.py','gate_material_clearance.py','gate_anatomy.py','gate_sealed_c_changes.py']
progress={'candidate':str(CANDIDATE),'candidate_sha256':CANDIDATE_SHA,'expected_saved_action':EXPECTED_ACTION,'read_only':True,'started_epoch_seconds':time.time(),'stages':[],'status':'running','visual_review':'required; not inferable from numerical gates'}
for stage in stages:
 t=time.time();print('GATE_STAGE_START',stage,flush=True)
 try:
  path=HERE/stage;namespace={'__file__':str(path),'__name__':'__main__'};exec(compile(path.read_text(),str(path),'exec'),namespace)
  for label,source,pin in INPUTS:assert sha(source)==pin,(label,'source changed during audit')
  progress['stages'].append({'name':stage,'status':'complete','elapsed_seconds':time.time()-t})
 except Exception as e:
  progress['stages'].append({'name':stage,'status':'failed','elapsed_seconds':time.time()-t,'error':str(e)});progress['status']='measurement_blocked';(OUTPUT/'GATE_PROGRESS.json').write_text(json.dumps(progress,indent=2)+'\n');raise
 (OUTPUT/'GATE_PROGRESS.json').write_text(json.dumps(progress,indent=2)+'\n');print('GATE_STAGE_DONE',stage,flush=True)
A=json.loads((OUTPUT/'A_static_skin_grip_audit.json').read_text());C=json.loads((OUTPUT/'C1_static_skin_grip_audit.json').read_text());material=json.loads((OUTPUT/'material_clearance_comparison.json').read_text());palmA=json.loads((OUTPUT/'A_palm_support_distribution.json').read_text());palmC=json.loads((OUTPUT/'C1_palm_support_distribution.json').read_text());anatomy=json.loads((OUTPUT/'anatomy_material_comparison.json').read_text());findings=[]
for side in ['Left','Right']:
 h=C['hands'][side];count=sum(x['samples_deeper_1mm'] for x in h['inside_by_region'].values())
 if count:findings.append(f'{side}: {count} parity-confirmed rifle samples deeper than1mm; depth metric requires caution if glove self-intersects')
 if h['nonlocal_cross_digit_exact_pairs']:findings.append(f'{side}: {len(h["nonlocal_cross_digit_exact_pairs"])} nonlocal cross-digit triangle pairs')
 old=anatomy['A']['sides'][side]['elbow_cap_vs_actual_arm_skin']['exact_triangle_crossing_pairs'];new=anatomy['C1']['sides'][side]['elbow_cap_vs_actual_arm_skin']['exact_triangle_crossing_pairs']
 if new>old:findings.append(f'{side} elbow cap/arm crossing pairs {old} to{new}')
nonlocal_count=material['C1']['material']['left_hand']['nonlocal_self_triangle_pairs']
if nonlocal_count:findings.append(f'Left glove: {nonlocal_count} nonlocal self-intersection pairs, including same-digit and palm checks')
invariants=json.loads((OUTPUT/'invariants_comparison.json').read_text())['C1']['comparison_to_A']
if not invariants['all_listed_mesh_rest_topology_weight_uv_hashes_equal'] or not invariants['rig_rest_hash_equal']:findings.append('Native mesh/rest/weight/UV or rig-rest preservation differs from A')
if C['hands']['Right']['comparison_to_A']['max_skin_delta_in_rifle_frame_mm']>.01:findings.append('Right grip evaluated skin changed by more than0.01mm in rifle coordinates; separate review required')
summary={'candidate':str(CANDIDATE),'sha256':CANDIDATE_SHA,'numeric_rejection_witnesses':findings,'numeric_checks_complete':True,'overall_status':'rejected_by_geometry' if findings else 'pending_actual_visual_and_support_review','central_palm_comparison':{band:{'A':palmA['Left']['bands'][band],'C1':palmC['Left']['bands'][band]} for band in ['central_25_50','central_50_75','distal_75_100']},'required_before_acceptance':['Actual matched physical-camera views show a more convincingly supported palm and finger wrap','Central palm support improves materially; larger distal overlap cannot count as support','Thumb opposition, nonlocal self-intersections, local material folds, anatomy and physical elbow-cap clearance reviewed together','Low penetration or lower wrist angle alone is never acceptance','No engine camera/eye-relief/shoulder acceptance follows from this gate']}
(OUTPUT/'NUMERIC_GATE_SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n');progress['status']='numeric_checks_complete_visual_and_crease_review_required';progress['completed_epoch_seconds']=time.time();(OUTPUT/'GATE_PROGRESS.json').write_text(json.dumps(progress,indent=2)+'\n');print('GATE_COMPLETE',json.dumps(summary['numeric_rejection_witnesses']),flush=True)
