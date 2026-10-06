from pathlib import Path
import json,hashlib,tarfile
r=Path(__file__).resolve().parent
j=json.loads((r/"PARTS_MANIFEST.json").read_text())
p=r/j["archive"]
with p.open("wb") as out:
 for part in j["parts"]:
  data=(r/part["file"]).read_bytes()
  assert len(data)==part["bytes"] and hashlib.sha256(data).hexdigest()==part["sha256"]
  out.write(data)
assert hashlib.sha256(p.read_bytes()).hexdigest()==j["archive_sha256"]
target=r/"rifle-visual-cleanup-r2-runtime"
with tarfile.open(p,"r:xz") as t:
 for m in t.getmembers():
  name=Path(m.name)
  assert not name.is_absolute() and ".." not in name.parts and not m.issym() and not m.islnk()
 t.extractall(target)
m=json.loads((target/"RUNTIME_MANIFEST.json").read_text())
for f in m["files"]:assert hashlib.sha256((target/f["file"]).read_bytes()).hexdigest()==f["sha256"]
print("Verified runtime review package:",target)
