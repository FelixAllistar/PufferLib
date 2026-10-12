"""Python 3 + Pillow: inspect runtime GLB image bytes and geometry accessors."""
import json,struct,hashlib,io,sys
from pathlib import Path
from PIL import Image
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
p=root/'runtime/wood_handle_flathead_screwdriver_250mm_MAIN.glb';raw=p.read_bytes();n,t=struct.unpack_from('<II',raw,12);g=json.loads(raw[20:20+n]);b=raw[28+n:];r={'file':str(p.relative_to(root)),'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'scenes':g.get('scenes'),'nodes':g['nodes'],'mesh_count':len(g['meshes']),'materials':g['materials'],'primitives':[],'images':[]}
for mesh in g['meshes']:
 for pr in mesh['primitives']:r['primitives'].append({'vertices':g['accessors'][pr['attributes']['POSITION']]['count'],'triangles':g['accessors'][pr['indices']]['count']//3,'attributes':list(pr['attributes'])})
for im in g['images']:
 v=g['bufferViews'][im['bufferView']];data=b[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']];img=Image.open(io.BytesIO(data));r['images'].append({'name':im.get('name'),'mimeType':im.get('mimeType'),'size':list(img.size),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
(root/'qa/glb_report.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
