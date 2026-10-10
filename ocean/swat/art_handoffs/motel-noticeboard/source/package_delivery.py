from pathlib import Path
import hashlib,json,zipfile
R=Path(__file__).resolve().parents[1];h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in R.rglob('*.blend1'):p.unlink()
files=sorted(p for p in R.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt')
(R/'SHA256SUMS.txt').write_text(''.join(f'{h(p)}  {p.relative_to(R).as_posix()}\n'for p in files))
z=R.parent/(R.name+'_CC0.zip')
with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as f:
 for p in sorted(R.rglob('*')):
  if p.is_file():
   info=zipfile.ZipInfo((R.name+'/'+p.relative_to(R).as_posix()),(2026,10,9,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o644<<16;f.writestr(info,p.read_bytes())
(z.parent/(z.name+'.sha256')).write_text(h(z)+'  '+z.name+'\n')
print(json.dumps({'archive':str(z),'bytes':z.stat().st_size,'sha256':h(z)},indent=2))
