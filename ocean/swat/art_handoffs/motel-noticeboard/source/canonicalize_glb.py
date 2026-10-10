"""Canonicalize triangle order only; Blender's exporter may emit unordered face iteration."""
import struct,json
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'runtime/motel_reception_noticeboard.glb';b=bytearray(p.read_bytes());n=struct.unpack_from('<I',b,12)[0];j=json.loads(b[20:20+n]);base=28+n
for m in j['meshes']:
 for pr in m['primitives']:
  assert pr.get('mode',4)==4;a=j['accessors'][pr['indices']];v=j['bufferViews'][a['bufferView']];fmt={5123:'H',5125:'I'}[a['componentType']];o=base+v.get('byteOffset',0)+a.get('byteOffset',0);ix=struct.unpack_from('<'+fmt*a['count'],b,o);tris=[]
  for k in range(0,len(ix),3):
   t=ix[k:k+3];tris.append(min(t,t[1:]+t[:1],t[2:]+t[:2]))
  vals=[i for t in sorted(tris) for i in t];struct.pack_into('<'+fmt*len(vals),b,o,*vals)
p.write_bytes(b)
