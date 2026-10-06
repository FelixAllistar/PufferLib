"""Replay the delivered static Crouch Ready fixture using Python 3 and NumPy.
Usage: python independent_validation/replay_fixture.py --package . --output replay.json
The independent Blender reference captures are included; Blender is only needed to recapture them.
"""
import argparse,json,subprocess,sys,tempfile,hashlib
from replay_static import GLB
from pathlib import Path

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--package',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();p=a.package.resolve();here=Path(__file__).resolve().parent
 glb=p/'crouch_ready_planted_shared_n_static.glb';source=here/'source_capture';reimport=here/'reimport_capture';refs=here/'reference_landmarks.json'
 report={'schema':'portable-independent-crouch-fixture-replay/1','status':'PASS','requirements':'Python3 and NumPy only','scope':'Independent bytes and fresh pinned Blender reference captures. Includes all vertices, all influences, joint FK, metadata, visor, bridge, soles and fresh stock rays. Reports separately supplied baseline identity checks; does not invent an authored temporal duration.'}
 with tempfile.TemporaryDirectory(prefix='crouch-static-replay-') as tmp:
  specs=[('binary','replay_static.py',['--glb',str(glb),'--source-capture',str(source),'--reimport-capture',str(reimport)]),('measurements','inspect_measurements.py',['--glb',str(glb),'--source-capture',str(source),'--references',str(refs)]),('metadata','inspect_metadata.py',['--package',str(p),'--source-capture',str(source),'--references',str(refs)])]
  for key,script,args in specs:
   out=Path(tmp)/(key+'.json');subprocess.run([sys.executable,str(here/script),*args,'--output',str(out)],check=True,stdout=subprocess.DEVNULL);report[key]=json.loads(out.read_text())
 baseline=json.loads((here/'static_baseline_manifest.json').read_text());g=GLB(glb);h=lambda v:hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 for k,v in baseline['static_json_sha256'].items():assert h(g.doc.get(k))==v,('static JSON differs',k)
 for row in baseline['accessors']:
  i=row['accessor'];assert h(g.doc['accessors'][i])==row['definition_sha256'] and hashlib.sha256(g.acc(i).astype('<f8').tobytes()).hexdigest()==row['decoded_array_sha256'],('static accessor differs',i)
 report['immutable_static_baseline']={'status':'PASS','baseline_glb_sha256':baseline['baseline_glb_sha256'],'static_accessors_checked':len(baseline['accessors']),'static_json_keys_checked':list(baseline['static_json_sha256'])}
 report['glb_sha256']=report['binary']['glb_sha256'];a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':'PASS','glb_sha256':report['glb_sha256'],'all_exported_vertices':report['binary']['source_parity']['total_exported_vertices'],'output':str(a.output)},indent=2))
if __name__=='__main__':main()
