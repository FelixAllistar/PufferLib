"""Read GLB inventories without Blender or exporters."""
import json,struct,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];out=[]
for f in ['motel_example.glb','motel_example_cutaway.glb']:
 raw=(ROOT/f).read_bytes();n=struct.unpack_from('<I',raw,12)[0];d=json.loads(raw[20:20+n]);tri=0
 for node in d['nodes']:
  if 'mesh' in node:
   for p in d['meshes'][node['mesh']]['primitives']:tri+=d['accessors'][p['indices']]['count']//3
 out.append({'file':f,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'placed_mesh_nodes':sum('mesh' in n for n in d['nodes']),'placed_triangles':tri,'unique_meshes':len(d['meshes']),'materials':len(d['materials']),'embedded_images':len(d['images']),'scene_extras':d['scenes'][d.get('scene',0)].get('extras',{}),'extensions_required':d.get('extensionsRequired',[]),'external_uris':[v['uri'] for k in ['images','buffers'] for v in d.get(k,[]) if 'uri' in v]})
okay=all(not v['external_uris'] and not v['extensions_required'] and v['scene_extras'].get('batch')=='15_motor_court_motel' and 'runtime_integration' not in v['scene_extras'] for v in out)
(ROOT/'qa/example_binary_summary.json').write_text(json.dumps({'status':'pass' if okay else 'fail','scope':'Example GLB node/triangle/material/image inventory and required-extension/external-URI dependency check. Fresh decoding and native transforms are tested separately.','examples':out},indent=2));print('Example binary inventory',okay,out)
if not okay:raise SystemExit(1)
