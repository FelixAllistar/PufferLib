"""Clean immutable payload allowlist, SHA-256 and archive read-back verification."""
from pathlib import Path
import json,hashlib,tarfile,zipfile
ROOT=Path(__file__).resolve().parent;prefix='neighborhood_storefronts_batch16_CC0'
m=json.loads((ROOT/'manifest.json').read_text());assert m['asset_count']==24 and len(m['reused_existing_assets'])==15
for name in ['source_provenance_report.json','independent_glb_report.json','independent_blender_report.json','portable_rebuild_report.json','presentation_report.json']:
 p=ROOT/'qa'/name;r=json.loads(p.read_text());assert r.get('status',r.get('result')) in ['pass','passed'],(name,r.get('status'))
# Reject stale PASS reports after a rebuild or final presentation save.
for name,h in json.loads((ROOT/'qa/independent_blender_report.json').read_text())['audited_input_sha256'].items():
 assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==h,('stale native audit',name)
for r in json.loads((ROOT/'qa/independent_glb_report.json').read_text())['files']:
 assert hashlib.sha256((ROOT/r['file']).read_bytes()).hexdigest()==r['sha256'],('stale binary audit',r['file'])
for r in json.loads((ROOT/'qa/portable_rebuild_report.json').read_text())['records']:
 assert hashlib.sha256((ROOT/r['file']).read_bytes()).hexdigest()==r['sha256_expected'],('stale rebuild audit',r['file'])
for r in json.loads((ROOT/'qa/presentation_report.json').read_text())['renders']:
 assert hashlib.sha256((ROOT/r['file']).read_bytes()).hexdigest()==r['sha256'],('stale presentation audit',r['file'])
required=['README.md','QA_SUMMARY.md','LICENSE.txt','requirements.txt','build_storefronts.py','geometry_core.py','lettering.py','render_storefronts.py','compose_sheets.py','package_batch.py','manifest.json','assembly_manifest.json','neighborhood_storefronts.blend','storefronts_example.glb','storefronts_example_cutaway.glb','storefronts_contact_sheet.jpg','storefronts_location_views.jpg','01_street_front.png','02_laundromat.png','03_pawn_repair.png','04_shared_service.png','05_cutaway.png','qa/visual_review.md']
files={ROOT/n for n in required}
for folder in ['assets','reused_assets','textures','previews','inputs','qa']:
 for p in (ROOT/folder).rglob('*'):
  if not p.is_file() or any(t.startswith('__') for t in p.parts) or p.suffix in ['.log','.pyc','.blend1']:continue
  if p.name in ['archive_integrity_report.json','payload_checksums.json']:continue
  files.add(p)
for p in files:assert p.is_file(),p
checks={p.relative_to(ROOT).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(files)}
(ROOT/'qa/payload_checksums.json').write_text(json.dumps({'schema':'artifact_payload_sha256_v1','files':checks},indent=2));files.add(ROOT/'qa/payload_checksums.json')
checksums={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
(ROOT/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n,h in checksums.items()));files.add(ROOT/'SHA256SUMS')
all_checks={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files};out=[]
for kind in ['zip','tar.xz']:
 path=ROOT/(prefix+'.'+kind)
 if kind=='zip':
  with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
   for p in sorted(files):z.write(p,prefix+'/'+p.relative_to(ROOT).as_posix())
  with zipfile.ZipFile(path) as z:
   assert z.testzip() is None;assert len(z.namelist())==len(set(z.namelist()))==len(files)
   for n in z.namelist():
    short=n.removeprefix(prefix+'/');assert short in all_checks and '..' not in Path(short).parts;assert hashlib.sha256(z.read(n)).hexdigest()==all_checks[short]
 else:
  with tarfile.open(path,'w:xz',preset=6) as z:
   for p in sorted(files):z.add(p,arcname=prefix+'/'+p.relative_to(ROOT).as_posix(),recursive=False)
  with tarfile.open(path,'r:xz') as z:
   assert len(z.getnames())==len(set(z.getnames()))==len(files)
   for member in z.getmembers():
    assert member.isfile();short=member.name.removeprefix(prefix+'/');assert short in all_checks and '..' not in Path(short).parts;assert hashlib.sha256(z.extractfile(member).read()).hexdigest()==all_checks[short]
 h=hashlib.sha256(path.read_bytes()).hexdigest();(ROOT/(path.name+'.sha256')).write_text(h+'  '+path.name+'\n');out.append({'file':path.name,'bytes':path.stat().st_size,'sha256':h,'verified_members':len(files)})
r={'status':'pass','scope':'All clean payload files read from ZIP and tar.xz and SHA-256 matched; ZIP CRC, unique safe paths, no backups/cache/logs/archives in payload.','archives':out};(ROOT/'qa/archive_integrity_report.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
