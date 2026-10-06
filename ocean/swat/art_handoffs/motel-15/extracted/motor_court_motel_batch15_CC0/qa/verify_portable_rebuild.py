"""Rebuild in a clean temporary directory with only documented portable inputs."""
from pathlib import Path
import tempfile,shutil,subprocess,json,hashlib,os
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='mc15_rebuild_') as td:
 dst=Path(td)
 for name in ['build_motel.py','geometry_core.py']:shutil.copy2(ROOT/name,dst/name)
 shutil.copytree(ROOT/'reused_assets',dst/'reused_assets')
 env=dict(os.environ,OMP_NUM_THREADS='2',OPENBLAS_NUM_THREADS='2')
 p=subprocess.run(['blender','-b','-t','2','--python-exit-code','1','--python',str(dst/'build_motel.py')],cwd=dst,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=300)
 if p.returncode:print(p.stdout[-6000:]);raise SystemExit(p.returncode)
 expected=json.loads((ROOT/'manifest.json').read_text());actual=json.loads((dst/'manifest.json').read_text());records=[]
 for a in expected['assets']:
  h=hashlib.sha256((dst/a['file']).read_bytes()).hexdigest();records.append({'file':a['file'],'status':'pass' if h==a['sha256'] else 'fail','rebuilt_sha256':h,'expected_sha256':a['sha256']})
 for f in ['motel_example.glb','motel_example_cutaway.glb']:
  h=hashlib.sha256((dst/f).read_bytes()).hexdigest();old=hashlib.sha256((ROOT/f).read_bytes()).hexdigest();records.append({'file':f,'status':'pass' if h==old else 'fail','rebuilt_sha256':h,'expected_sha256':old})
 okay=all(r['status']=='pass' for r in records)
 report={'status':'pass' if okay else 'fail','scope':'Isolated full regeneration from build_motel.py, geometry_core.py and packaged reused_assets only; no other batch directories. Exact SHA-256 match for every new module and both complete example binaries. Native .blend container bytes and previews excluded.','files':records}
 (ROOT/'qa/portable_rebuild_report.json').write_text(json.dumps(report,indent=2));print(report['status'])
 if not okay:raise SystemExit(1)
