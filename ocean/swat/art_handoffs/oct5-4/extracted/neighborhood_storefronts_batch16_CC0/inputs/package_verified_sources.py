"""Copy selected prior GLBs and current material package without modifying originals."""
from pathlib import Path
import hashlib,json,shutil,struct,datetime
import numpy as np
ROOT=Path('/workspace/scratch/a07b17ad341f')
OUT=ROOT/'environment_batches/16_neighborhood_storefronts'
SELECTION={
 '11_laundry_basement':['front_load_washer','vented_tumble_dryer','folded_bedsheet_stack','deep_utility_sink','handled_detergent_jug'],
 '04_furniture_variants':['laundry_hamper_woven','table_folding_card','shelving_steel_repaired'],
 '09_garage_workshop':['workbench_pegboard','corded_power_drill','combination_pliers'],
 '02_household_clutter':['portable_am_fm_radio','two_slot_toaster','twin_bell_alarm_clock'],
}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def glb_info(path):
 raw=path.read_bytes();magic,version,length=struct.unpack_from('<4sII',raw)
 assert magic==b'glTF' and version==2 and length==len(raw)
 off=12;chunks={}
 while off<len(raw):
  n,k=struct.unpack_from('<II',raw,off);off+=8;chunks[k]=raw[off:off+n];off+=n
 doc=json.loads(chunks[0x4e4f534a]);binary=chunks[0x004e4942]
 external=[x['uri'] for section in ['buffers','images'] for x in doc.get(section,[]) if 'uri'in x and not x['uri'].startswith('data:')]
 assert not external
 def pos(acc_id):
  a=doc['accessors'][acc_id];v=doc['bufferViews'][a['bufferView']];assert a['componentType']==5126 and a['type']=='VEC3' and 'sparse'not in a
  return np.ndarray((a['count'],3),dtype='<f4',buffer=binary,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',12),4)).astype(float)
 def transform(node):
  if 'matrix'in node:return np.array(node['matrix']).reshape(4,4).T
  x,y,z,w=node.get('rotation',[0,0,0,1]);r=np.array([[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],[2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],[2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]])
  m=np.eye(4);m[:3,:3]=r@np.diag(node.get('scale',[1,1,1]));m[:3,3]=node.get('translation',[0,0,0]);return m
 mins=[];maxs=[];tris=0
 def visit(idx,parent):
  nonlocal tris
  node=doc['nodes'][idx];m=parent@transform(node)
  if 'mesh'in node:
   for p in doc['meshes'][node['mesh']]['primitives']:
    a=pos(p['attributes']['POSITION']);w=a@m[:3,:3].T+m[:3,3];mins.append(w.min(0));maxs.append(w.max(0))
    assert p.get('mode',4)==4
    tris+=(doc['accessors'][p['indices']]['count'] if 'indices'in p else len(a))//3
  for c in node.get('children',[]):visit(c,m)
 for node in doc['scenes'][doc.get('scene',0)]['nodes']:visit(node,np.eye(4))
 low=np.min(mins,0);high=np.max(maxs,0)
 source_lo=[low[0],-high[2],low[1]];source_hi=[high[0],-low[2],high[1]]
 return {'gltf_version':version,'external_uris':external,'embedded_image_count':len(doc.get('images',[])),'triangles':tris,'bounds_m_gltf_xyz':[low.tolist(),high.tolist()],'bounds_m_source_xyz':[source_lo,source_hi],'dimensions_m_source_xyz':(np.array(source_hi)-source_lo).tolist(),'dimensions_m_gltf_xyz':(high-low).tolist()}
assets=[];evidence=[]
for batch,names in SELECTION.items():
 source_dir=ROOT/'environment_batches'/batch
 manifest=json.loads((source_dir/'manifest.json').read_text())
 proofdir=OUT/'inputs/prior_asset_provenance'/batch;proofdir.mkdir(parents=True,exist_ok=True)
 for docname in ['manifest.json','LICENSE.txt']:
  s=source_dir/docname;d=proofdir/docname;shutil.copy2(s,d);assert sha(s)==sha(d)
  evidence.append({'source':str(s.relative_to(ROOT)),'copy':str(d.relative_to(OUT)),'sha256':sha(d)})
 for name in names:
  record=next(a for a in manifest['assets'] if a.get('name',a.get('asset_id'))==name)
  source=source_dir/record['file'];dest=OUT/'reused_assets'/source.name;shutil.copy2(source,dest)
  digest=sha(source);assert digest==sha(dest)
  assert not record.get('sha256') or digest==record['sha256']
  info=glb_info(dest)
  original_bounds=record.get('bounds_m_source_xyz',record.get('source_bounds_z_up'))
  if original_bounds is None and 'aabb_blender_z_up'in record:original_bounds=[record['aabb_blender_z_up']['min'],record['aabb_blender_z_up']['max']]
  assert original_bounds is not None
  delta=float(np.max(np.abs(np.array(original_bounds)-info['bounds_m_source_xyz'])))
  assert delta<1e-4,(name,delta)
  asset={'id':record.get('id',record.get('asset_id',name)),'name':name,'title':record.get('title',record.get('label',name.replace('_',' ').title())),'file':str(dest.relative_to(OUT)),'original_source_path':str(source.relative_to(ROOT)),'original_batch':batch,'source_manifest':str((source_dir/'manifest.json').relative_to(ROOT)),'packaged_source_manifest':str((proofdir/'manifest.json').relative_to(OUT)),'source_manifest_asset_id':record.get('id',record.get('asset_id',name)),'license':'CC0-1.0','license_evidence':str((proofdir/'LICENSE.txt').relative_to(OUT)),'license_url':'https://creativecommons.org/publicdomain/zero/1.0/legalcode','cc0_source':'Original authored geometry and texture artwork; prior batch CC0 dedication preserved unmodified.','sha256':digest,'bytes':dest.stat().st_size,'copy_byte_identical':True,'source_manifest_sha256_match':digest==record['sha256'] if record.get('sha256') else None,'documented_bounds_m_source_xyz':original_bounds,'documented_vs_measured_bound_max_delta_m':delta,**info,'source_coordinates':'right-handed Z-up; front -Y','gltf_coordinates':'right-handed Y-up; front +Z','collider_enabled':False}
  if name=='table_folding_card':asset['reuse_note']='Existing folding card table used as a laundry folding surface; no dedicated laundry folding table exists in inspected prior batches. No geometry or materials changed.'
  assets.append(asset)
catalog={'schema':'neighborhood_storefronts_reuse_catalog_v1','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'asset_count':len(assets),'copy_policy':'Full byte-identical prior GLBs; original IDs retained; no mesh/material re-export or edits.','units':'metres','basis_note':'Measured bounds are computed from full glTF scene node transforms; source coordinates use inverse exporter mapping (x,y,z)_source=(x,-z,y)_gltf.','assets':assets}
(OUT/'reused_assets/reuse_catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
# Preserve the complete newest material candidate family, except QA render images and caches.
msrc=ROOT/'pufferlib_swat_materials/ocean/swat/assets/environment/materials_v1';mdst=OUT/'inputs/materials_v1'
material_files=[];omitted=[]
for s in sorted(msrc.rglob('*')):
 if not s.is_file():continue
 rel=s.relative_to(msrc)
 if '__pycache__'in rel.parts or ('qa'in rel.parts and s.suffix.lower()in ['.jpg','.jpeg','.png','.exr']):omitted.append(str(rel));continue
 d=mdst/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s,d);assert sha(s)==sha(d)
 material_files.append({'file':str(rel),'bytes':d.stat().st_size,'sha256':sha(d),'copy_byte_identical':True})
m=json.loads((mdst/'material_manifest.json').read_text())
for mat in m['materials']:
 for channel,rec in mat['maps'].items():assert sha(mdst/rec['file'])==rec['sha256']
for path,digest in m['source_files'].items():assert sha(mdst/path)==digest
report={'schema':'storefront_source_provenance_verification_v1','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'result':'passed','reused_asset_count':len(assets),'all_glbs_byte_identical_to_prior_sources':all(a['copy_byte_identical']for a in assets),'all_glbs_embedded_self_contained':all(not a['external_uris']for a in assets),'all_measured_bounds_match_documentation_within_m':0.0001,'source_evidence':evidence,'assets':assets,'material_family':{'original_source_path':str(msrc.relative_to(ROOT)),'copy_path':str(mdst.relative_to(OUT)),'material_ids':[x['id']for x in m['materials']],'runtime_map_count':sum(len(x['maps'])for x in m['materials']),'all_map_and_source_hashes_match_manifest':True,'copied_files':material_files,'omitted_files':omitted,'omission_reason':'Only offline QA render images and Python caches omitted; all runtime maps, editable source files, three original scan JPEGs, provenance, licensing and QA JSON evidence retained.','license_distinction':'Five original procedural families are CC0 per preserved LICENSE_CC0.txt; floor_pine is a derivative of the separately CC0 Poly Haven Wood Floor Worn scan. Earlier provisional imagegen painted_plaster_basecolor_v1.png is outside this family, is not copied, and is not relabeled CC0.','external_source_provenance':m['external_source_provenance']},'prior_sources_modified':False,'scope':'Asset-only preparation; no engine, runtime, collider, simulation or gameplay integration.'}
(OUT/'qa/source_provenance_report.json').write_text(json.dumps(report,indent=2)+'\n')
(OUT/'inputs/SOURCE_PROVENANCE.md').write_text('# Source provenance\n\nFourteen full GLBs are byte-identical copies of prior original CC0 batches. See ../reused_assets/reuse_catalog.json for exact IDs, hashes, dimensions, and source paths. Original manifests and license texts are preserved in prior_asset_provenance/.\n\nmaterials_v1/ contains all six latest material families, eighteen runtime maps, editable source scripts, original CC0 scan inputs, licensing, manifests, and prior QA reports. QA render images and caches are omitted. Files copied from the source package are unmodified.\n\nThe older provisional imagegen plaster is neither copied nor covered by this CC0 claim. This package preserves the explicit distinction in materials_v1/LICENSE_CC0.txt. New procedural plaster candidates are separate original procedural works with their own dedication.\n\nThe folding card table is reused unchanged as a folding surface; no dedicated laundry folding table was found. Every reused object remains art-only with no collider or gameplay binding.\n')
print(json.dumps({'asset_count':len(assets),'assets':[{k:a[k]for k in ['id','file','dimensions_m_source_xyz']}for a in assets],'material_files_copied':len(material_files),'material_maps':18,'result':'passed'},indent=2))
