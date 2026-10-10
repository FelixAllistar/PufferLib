"""Create compact delivery previews and archive; PNGs remain local render masters."""
from pathlib import Path
from PIL import Image
import zipfile, hashlib, json
ROOT=Path(__file__).resolve().parent
# Wait for all renders externally; never package a partial staged set.
required=['00_contact_sheet','01_reading_room','02_bedroom_storage','03_utility_corner']
assert all((ROOT/'previews'/(n+'.png')).is_file() for n in required),'Missing catalogue or staged image'
for p in (ROOT/'previews').glob('*.png'):
 Image.open(p).convert('RGB').save(p.with_suffix('.jpg'),quality=94,subsampling=0,optimize=True)
include=[]
for p in ROOT.rglob('*'):
 if not p.is_file():continue
 rel=p.relative_to(ROOT)
 if p.suffix in ('.log','.zip') or '__pycache__' in p.parts or (rel.parts[0]=='previews' and p.suffix=='.png'):continue
 include.append(p)
archive=ROOT/'furniture_variants_batch04.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(include):z.write(p,'04_furniture_variants/'+p.relative_to(ROOT).as_posix())
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
print(json.dumps({'archive':str(archive),'files':len(include),'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()},indent=2))
