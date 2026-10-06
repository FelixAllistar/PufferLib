from pathlib import Path
import json,hashlib,tarfile
O=Path(__file__).resolve().parent;ROOT=O.parents[2];R=ROOT/'animation_rebuild';files=[]
def add(p):
 p=Path(p)
 if p.is_file() and p not in files:files.append(p)
def selected_tree(p):
 for f in sorted(Path(p).rglob('*')):
  if f.is_file() and '__pycache__' not in f.parts and f.suffix not in ['.png','.log','.pyc'] and 'replay_output' not in f.parts and 'replay_audit' not in f.parts:add(f)
selected_tree(O.parent/'candidate_c1');selected_tree(O.parent/'candidate_c1_review')
for f in O.iterdir():
 if f.is_file() and f.suffix in ['.py','.md','.jpg','.json'] and f.name not in ['MANIFEST.json','SEALED_CHECKPOINT.json','REMOTE_BACKUP.json','FRESH_EXTRACTION_REPLAY_RECEIPT.json']:add(f)
selected_tree(O/'review_images');add(O/'replay_output/REPLAY_RECEIPT.json')
for n in ['support_grip_surface_c.editable.blend','CANDIDATE_C_AUTHORING.json','EXACT_LEFT_GRIP_HANDOFF.json','source_pose_readonly.json']:add(O.parent/'candidate_c'/n)
for n in ['low_area_face_comparison.json','CANDIDATE_C_REVIEW.md','FINAL_DISPOSITION.json','crease_views/CREASE_VIEW_DEFINITIONS.json']:add(O.parent/'candidate_c_review'/n)
for n in ['upper_body_gear_rebuild/stage_f/upper_gear_stage_f.editable.blend','upper_body_gear_rebuild/stage_f/COMPATIBILITY_AND_BINDINGS.json','upper_body_gear_rebuild/stage_f/GEOMETRY_UV_MATERIAL_BINDINGS.json','upper_body_gear_rebuild/stage_f/EXPORT_INTENT.json','shared_pose_fit/portable_recipe/native_frame_reference.json','rigid_weapon_package/package/rifle_bindings.json','eye_visor_landmark/optical_landmark_source.json']:add(R/n)
add(O.parent/'source_inspection/F/VIEW_DEFINITIONS.json');add(O.parent/'source_inspection/ENGINE_LANDMARK_COMPARISON.json')
m={'status':'FROZEN DIAGNOSTIC ONLY. User moved final thumb/finger fitting to the actual in-game ADS view. Thumb-wrap intent unresolved. Preserve current engine pose; no automatic C/C1 promotion.','editable_sha256':'17c1c13ffbea9d8fac459914eb2bbb1c6c336b785f9382450002f0ad6a53ac99','files':{str(p.relative_to(ROOT)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}for p in sorted(files)}}
(O/'MANIFEST.json').write_text(json.dumps(m,indent=2)+'\n');add(O/'MANIFEST.json');archive=O/'support-grip-c1-frozen-diagnostic-checkpoint.tar.gz'
with tarfile.open(archive,'w:gz',compresslevel=6)as t:
 for p in sorted(files):t.add(p,arcname=str(p.relative_to(ROOT)),recursive=False)
with tarfile.open(archive)as t:
 for n,row in m['files'].items():b=t.extractfile(n).read();assert len(b)==row['bytes'] and hashlib.sha256(b).hexdigest()==row['sha256']
p=O/'support-grip-c1-matched-comparison.jpg';receipt={'archive':str(archive),'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'payload_files':len(m['files']),'panel':{'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}};(O/'SEALED_CHECKPOINT.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
