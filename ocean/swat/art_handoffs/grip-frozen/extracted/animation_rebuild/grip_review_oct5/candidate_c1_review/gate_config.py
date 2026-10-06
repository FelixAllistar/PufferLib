"""Input pinning for an explicitly supplied immutable candidate; no authoring."""
import argparse,hashlib,re,sys
from pathlib import Path
ROOT=Path('/workspace/scratch/eac4961518d4/animation_rebuild')
OUTPUT=ROOT/'grip_review_oct5/candidate_c1_review'
p=argparse.ArgumentParser(description='Read-only candidate C1 actual-skin review. Requires exact authorized source and SHA; never saves Blender assets.')
p.add_argument('--candidate',required=True,type=Path)
p.add_argument('--sha256',required=True)
p.add_argument('--expected-action',default=None)
args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
CANDIDATE=args.candidate.resolve();CANDIDATE_SHA=args.sha256.lower();EXPECTED_ACTION=args.expected_action
assert re.fullmatch('[0-9a-f]{64}',CANDIDATE_SHA),'SHA256 must contain64 hexadecimal characters'
assert CANDIDATE.is_file() and CANDIDATE.suffix=='.blend','Exact immutable .blend input is required'
BASELINE=ROOT/'upper_body_gear_rebuild/stage_f/upper_gear_stage_f.editable.blend'
BASELINE_SHA='4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0'
assert CANDIDATE!=BASELINE,'Candidate must be a separate file'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
INPUTS=[('A',BASELINE,BASELINE_SHA),('C1',CANDIDATE,CANDIDATE_SHA)]
for label,path,pin in INPUTS:assert sha(path)==pin,(label,'source SHA256 mismatch')
def verify_loaded(bpy,label):
 rig=bpy.data.objects.get('SWAT_Mixamo_Rig');assert rig and rig.animation_data and rig.animation_data.action,'Saved active action is required'
 if label=='A':assert rig.animation_data.action.name=='Neutral Carry / Anatomical Gear F'
 if label=='C1' and EXPECTED_ACTION:assert rig.animation_data.action.name==EXPECTED_ACTION,'Saved action differs from requested action; no action is silently switched'

SEALED_C=ROOT/'grip_review_oct5/candidate_c/support_grip_surface_c.editable.blend'
SEALED_C_SHA='f1b39d78b32da500f9fd4370d942d364c49a1d9d788ac1122282b4275e371dcb'
assert sha(SEALED_C)==SEALED_C_SHA,'Sealed C comparison source changed'
