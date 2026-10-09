"""Build a compact local archive with deterministic member timestamps and hash manifest."""
from pathlib import Path
import hashlib,json,zipfile
R=Path(__file__).resolve().parents[1]
paths=sorted(p for p in R.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt' and not p.name.endswith('.blend1'))
(R/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(R))+'\n' for p in paths))
out=R.parent/(R.name+'_CC0.zip')
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(paths+[R/'SHA256SUMS.txt']):
  info=zipfile.ZipInfo(R.name+'/'+str(p.relative_to(R)),(2026,10,9,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o644<<16;z.writestr(info,p.read_bytes())
sha=hashlib.sha256(out.read_bytes()).hexdigest();out.with_suffix(out.suffix+'.sha256').write_text(sha+'  '+out.name+'\n')
print(json.dumps({'path':str(out),'bytes':out.stat().st_size,'sha256':sha},indent=2))
