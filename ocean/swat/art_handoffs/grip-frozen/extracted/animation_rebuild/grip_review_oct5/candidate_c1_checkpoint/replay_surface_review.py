"""Portable read-only replay of archived numeric surface gates in a separate output folder.
blender -b -t 2 --python /extracted/.../candidate_c1_checkpoint/replay_surface_review.py
Outputs do not replace the archived selected reports. Visual findings remain authored review.
"""
import sys,json,hashlib
from pathlib import Path
HERE=Path(__file__).resolve().parent
for ancestor in [HERE,*HERE.parents]:
    if (ancestor/'animation_rebuild/upper_body_gear_rebuild/stage_f/upper_gear_stage_f.editable.blend').is_file():ROOT=ancestor;break
else:raise RuntimeError('Preserved project tree is missing')
R=ROOT/'animation_rebuild';source=R/'grip_review_oct5/candidate_c1_review';out=HERE/'replay_audit';out.mkdir(exist_ok=True)
names=['run_gate.py','gate_config.py','gate_invariants.py','gate_skin_contacts.py','gate_palm_distribution.py','gate_skin_finger_axes.py','gate_material_clearance.py','gate_anatomy.py','gate_sealed_c_changes.py']
for name in names:
    text=(source/name).read_text().replace('/workspace/scratch/eac4961518d4/animation_rebuild',str(R)).replace('/workspace/shared/practical_ads_audit_recovery_algorithms.py',str(HERE/'surface_algorithms.py')).replace('grip_review_oct5/candidate_c1_review','grip_review_oct5/candidate_c1_checkpoint/replay_audit')
    (out/name).write_text(text)
report=json.loads((R/'grip_review_oct5/candidate_c1/CANDIDATE_C1_AUTHORING.json').read_text());sys.argv=['blender','--','--candidate',str(R/'grip_review_oct5/candidate_c1/support_grip_index_release_c1.editable.blend'),'--sha256',report['output_sha256'],'--expected-action',report['action']]
sys.path.insert(0,str(out));script=out/'run_gate.py';exec(compile(script.read_text(),str(script),'exec'),{'__file__':str(script),'__name__':'__main__'})
