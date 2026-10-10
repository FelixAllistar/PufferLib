"""Dependency-free GLB structure, image, primitive and metadata checks."""
import json, pathlib, struct, hashlib
ROOT=pathlib.Path(__file__).resolve().parent
manifest=json.loads((ROOT/'manifest.json').read_text());errors=[];rows=[]
for asset in manifest['assets']:
 path=ROOT/asset['file'];raw=path.read_bytes()
 magic,version,length=struct.unpack_from('<4sII',raw)
 if magic!=b'glTF' or version!=2 or length!=len(raw):errors.append(path.name+': invalid header')
 n,kind=struct.unpack_from('<II',raw,12);doc=json.loads(raw[20:20+n]);bins=raw[20+n:]
 if any('uri' in b for b in doc.get('buffers',[])):errors.append(path.name+': external buffer')
 if any('uri' in im or 'bufferView' not in im for im in doc.get('images',[])):errors.append(path.name+': external image')
 triangle_count=sum(doc['accessors'][p['indices']]['count']//3 for m in doc.get('meshes',[]) for p in m.get('primitives',[]))
 if triangle_count!=asset['triangles']:errors.append(path.name+': triangle count mismatch')
 for mesh in doc.get('meshes',[]):
  for p in mesh['primitives']:
   if p.get('mode',4)!=4:errors.append(path.name+': unexpected primitive mode')
   if 'TEXCOORD_0' not in p['attributes']:errors.append(path.name+': absent UVs')
 for m in doc.get('materials',[]):
  pbr=m.get('pbrMetallicRoughness',{})
  if 'baseColorTexture' in pbr and pbr.get('baseColorFactor',[1,1,1,1])!=[1,1,1,1]:errors.append(path.name+': tinted image texture factor')
 if not any(n.get('extras',{}).get('asset_id')==asset['asset_id'] for n in doc.get('nodes',[])):errors.append(path.name+': missing asset extras')
 rows.append({'asset_id':asset['asset_id'],'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'triangles':triangle_count,'embedded_images':len(doc.get('images',[])),'materials':len(doc.get('materials',[]))})
report={'status':'pass' if not errors else 'fail','asset_count':len(rows),'total_triangles':sum(a['triangles'] for a in rows),'errors':errors,'files':rows,'scope':'GLB data integrity; not runtime or collision certification'}
(ROOT/'qa'/'validation_report.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2))
raise SystemExit(bool(errors))
