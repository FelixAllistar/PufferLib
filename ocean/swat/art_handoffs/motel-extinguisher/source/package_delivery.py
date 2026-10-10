from pathlib import Path
import json,hashlib,zipfile
R=Path(__file__).resolve().parents[1];H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=json.loads((R/'qa/runtime_audit.json').read_text());assert H(R/'runtime/motel_fire_extinguisher.glb')==a['sha256']==json.loads((R/'qa/reimport.json').read_text())['runtime_sha256'];assert json.loads((R/'qa/rebuild_verification.json').read_text())['byte_identical']
files=sorted(p for p in R.rglob('*')if p.is_file() and p.name!='SHA256SUMS.txt' and not p.name.endswith('.blend1') and '__pycache__' not in p.parts)
(R/'SHA256SUMS.txt').write_text(''.join(f'{H(p)}  {p.relative_to(R).as_posix()}\n'for p in files));files.append(R/'SHA256SUMS.txt')
zpath=R.parent/(R.name+'_CC0.zip')
with zipfile.ZipFile(zpath,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9)as z:
 for p in files:
  info=zipfile.ZipInfo(R.name+'/'+p.relative_to(R).as_posix(),(2026,10,9,22,35,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=(0o755 if p.suffix=='.sh' else 0o644)<<16;z.writestr(info,p.read_bytes())
with zipfile.ZipFile(zpath)as z:assert z.testzip()is None
assert zpath.stat().st_size<32*1024*1024
(zpath.parent/(zpath.name+'.sha256')).write_text(f'{H(zpath)}  {zpath.name}\n');out={'file':str(zpath),'bytes':zpath.stat().st_size,'sha256':H(zpath),'archive_crc_pass':True,'runtime_sha256':a['sha256'],'delivery':'local only; no upload/post'};(R.parent/(R.name+'_package_receipt.json')).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
