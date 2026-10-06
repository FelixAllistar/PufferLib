"""Clean package allowlist with payload hashes and full archive read-back verification."""
from pathlib import Path
import json,hashlib,tarfile,zipfile,io
ROOT=Path(__file__).resolve().parent;prefix='motor_court_motel_batch15_CC0'
manifest=json.loads((ROOT/'manifest.json').read_text())
assert manifest['asset_count']==24 and len(manifest['reused_existing_assets'])==16
reports=['qa/independent_glb_report.json','qa/reimport_route_report.json','qa/assembly_connection_report.json','qa/portable_rebuild_report.json','qa/presentation_report.json']
for f in reports:
 r=json.loads((ROOT/f).read_text());assert r['status'] in ['pass','passed'],(f,r['status'])
required=['README.md','LICENSE.txt','requirements.txt','build_motel.py','geometry_core.py','render_motel.py','compose_sheets.py','package_batch.py','finalize_scene_metadata.py','manifest.json','assembly_manifest.json','motor_court_motel.blend','motel_example.glb','motel_example_cutaway.glb','motel_courtyard.png','motel_cutaway.png','motel_guest_room.png','motel_reception.png','motel_contact_sheet.jpg','motel_location_views.jpg','qa/visual_review.md']
files={ROOT/n for n in required}
for folder,suffixes in [('assets',{'.glb'}),('reused_assets',{'.glb'}),('textures',{'.png','.json'}),('previews',{'.png'}),('qa',{'.py','.json'})]:
 files|={p for p in (ROOT/folder).iterdir() if p.suffix in suffixes and p.name!='archive_integrity_report.json'}
files.discard(ROOT/'qa/payload_checksums.json')
for p in files:assert p.is_file(),p
checks={p.relative_to(ROOT).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(files)}
(ROOT/'qa/payload_checksums.json').write_text(json.dumps({'schema':'artifact_payload_sha256_v1','files':checks},indent=2));files.add(ROOT/'qa/payload_checksums.json')
all_checks={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files};out=[]
for kind in ['tar.xz','zip']:
 path=ROOT/(prefix+'.'+kind)
 if kind=='tar.xz':
  with tarfile.open(path,'w:xz',preset=6) as arc:
   for p in sorted(files):arc.add(p,arcname=prefix+'/'+p.relative_to(ROOT).as_posix(),recursive=False)
  with tarfile.open(path,'r:xz') as arc:
   names=arc.getnames();assert len(names)==len(set(names))==len(files)
   for member in arc.getmembers():
    assert member.isfile();name=member.name.removeprefix(prefix+'/');assert name in all_checks and '..' not in Path(name).parts;assert hashlib.sha256(arc.extractfile(member).read()).hexdigest()==all_checks[name]
 else:
  with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as arc:
   for p in sorted(files):arc.write(p,prefix+'/'+p.relative_to(ROOT).as_posix())
  with zipfile.ZipFile(path) as arc:
   assert arc.testzip() is None;names=arc.namelist();assert len(names)==len(set(names))==len(files)
   for n in names:
    name=n.removeprefix(prefix+'/');assert name in all_checks and '..' not in Path(name).parts;assert hashlib.sha256(arc.read(n)).hexdigest()==all_checks[name]
 h=hashlib.sha256(path.read_bytes()).hexdigest();(ROOT/(path.name+'.sha256')).write_text(h+'  '+path.name+'\n');out.append({'file':path.name,'bytes':path.stat().st_size,'sha256':h,'verified_members':len(files)})
report={'status':'pass','scope':'All whitelisted files read back from both archives and SHA-256 compared; ZIP CRC, unique safe paths, no caches/logs/backups or embedded archive files.','archives':out};(ROOT/'qa/archive_integrity_report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
