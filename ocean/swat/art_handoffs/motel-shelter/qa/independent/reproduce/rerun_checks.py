import sys,subprocess,shutil,json,hashlib,os
from pathlib import Path
D=Path(__file__).parent;C=Path(sys.argv[1]).resolve();expected=sys.argv[2]
p=C/'runtime/BC_SHELTER01_static.glb';actual=hashlib.sha256(p.read_bytes()).hexdigest();assert actual==expected,(actual,expected)
for q in ['snapshot/runtime','packed_only','relocated/source/maps']:(D/q).mkdir(parents=True,exist_ok=True)
shutil.copy2(p,D/'snapshot/runtime'/p.name);shutil.copy2(C/'source/BC_SHELTER01_editable.blend',D/'packed_only/asset.blend')
for n in ['export_packed.py','build_shelter.py','make_textures.py','render_export.py']:shutil.copy2(C/'source'/n,D/'relocated/source'/n)
shutil.copy2(C/'source/export_packed.py',D/'packed_only/export_packed.py')
for q in (C/'source/maps').iterdir():
 if q.is_file():shutil.copy2(q,D/'relocated/source/maps'/q.name)
def run(cmd,log,env=None):
 with open(D/log,'w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,env=env)
env=dict(os.environ,SHELTER_OUT=str(D/'regenerated_textures'))
run([sys.executable,str(D/'relocated/source/make_textures.py')],'texture_rebuild.log',env)
textures=[]
for t in (D/'regenerated_textures/source/maps').glob('*.png'):
 a=hashlib.sha256(t.read_bytes()).hexdigest();b=hashlib.sha256((C/'source/maps'/t.name).read_bytes()).hexdigest();assert a==b
 textures.append({'file':t.name,'regenerated_sha256':a,'candidate_sha256':b,'byte_identical':a==b})
(D/'texture_recipe_comparison.json').write_text(json.dumps(textures,indent=2))
run([sys.executable,str(D/'audit_glb.py'),str(D/'snapshot/runtime'/p.name),str(D/'runtime_audit.json')],'runtime_audit.log')
run(['blender','-b','-t','2','--python',str(D/'packed_only/export_packed.py'),'--',str(D/'packed_only/asset.blend'),str(D/'packed_only/reexport.glb')],'packed_only/reexport.log')
env=dict(os.environ,SHELTER_OUT=str(D/'relocated'));run(['blender','-b','-t','2','--python',str(D/'relocated/source/build_shelter.py')],'relocated/rebuild.log',env)
for n,p2 in [('packed_helper',D/'packed_only/reexport.glb'),('relocated',D/'relocated/runtime'/p.name)]:
 run([sys.executable,str(D/'compare_surfaces.py'),str(D/'snapshot/runtime'/p.name),str(p2)],n+'_compare.log');shutil.copy2(D/'packed_surface_comparison.json',D/(n+'_comparison.json'))
run([sys.executable,str(D/'audit_materials.py')],'material_comparison.log')
run(['blender','-b','-t','2','--python',str(D/'inspect_source.py')],'source_inspection.log')
run(['blender','-b','-t','2','--python',str(D/'scan_coplanar.py')],'coplanar_scan.log')
assert hashlib.sha256(p.read_bytes()).hexdigest()==expected,'Candidate changed during audit'
print('CHECKS_COMPLETE',expected)
