"""Full isolated regeneration, no parent-workspace or earlier batch dependencies."""
from pathlib import Path
import tempfile,shutil,subprocess,json,hashlib,os
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='ns16_isolated_') as td:
 dst=Path(td)
 for name in ['build_storefronts.py','geometry_core.py','lettering.py']:shutil.copy2(ROOT/name,dst/name)
 for name in ['inputs','reused_assets']:shutil.copytree(ROOT/name,dst/name)
 env=dict(os.environ,OMP_NUM_THREADS='2',OPENBLAS_NUM_THREADS='2')
 p=subprocess.run(['blender','-b','-t','2','--python-exit-code','1','--python',str(dst/'build_storefronts.py')],cwd=dst,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=360)
 (ROOT/'qa/portable_rebuild.log').write_text(p.stdout)
 if p.returncode:print(p.stdout[-3000:]);raise SystemExit(p.returncode)
 m=json.loads((ROOT/'manifest.json').read_text());rows=[]
 for name in [a['file'] for a in m['assets']]+['storefronts_example.glb','storefronts_example_cutaway.glb']:
  expected=hashlib.sha256((ROOT/name).read_bytes()).hexdigest();actual=hashlib.sha256((dst/name).read_bytes()).hexdigest();rows.append({'file':name,'status':'pass' if actual==expected else 'fail','sha256_expected':expected,'sha256_rebuilt':actual})
 report={'status':'pass' if all(r['status']=='pass' for r in rows) else 'fail','scope':'Isolated generation with only included scripts, inputs and byte-identical reused_assets. Every new module and both example GLBs must reproduce SHA-256 exactly. Native .blend container byte layout, renders and timestamps excluded.','records':rows}
 (ROOT/'qa/portable_rebuild_report.json').write_text(json.dumps(report,indent=2));print(report['status']);assert report['status']=='pass'
