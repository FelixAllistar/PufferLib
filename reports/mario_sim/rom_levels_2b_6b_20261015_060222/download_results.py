from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tarfile

paths = json.loads(Path(Path('/tmp/mario_level_eval_paths').read_text()).read_text())
root = Path(paths['local'])
script = 'from pathlib import Path\nimport hashlib,json,tarfile,time,subprocess\n'
script += 'root=Path(' + repr(paths['remote']) + ')\n'
script += r'''
assert (root/'results/summary.json').exists(), 'evaluation is not finished'
for _ in range(20):
    running = subprocess.run(['tmux','has-session','-t','mario-rom-levels-060222'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not running: break
    time.sleep(.1)
assert not running, 'evaluation has not exited yet'
archive=root/'results.tar.gz'
with tarfile.open(archive,'w:gz') as stream:
    for name in ('results','launch.json','cpu_limit.json','evaluation.log'):
        stream.add(root/name,arcname=name)
with archive.open('rb') as stream:
    sha=hashlib.file_digest(stream,'sha256').hexdigest()
print(json.dumps({'path':str(archive),'sha256':sha,'size':archive.stat().st_size}))
'''
remote = subprocess.run(['ssh', '-p', '25023', 'root@ssh5.vast.ai', 'python3', '-'],
                        input=script, text=True, capture_output=True, check=True)
manifest = json.loads(remote.stdout.splitlines()[-1])
archive = root / 'results.tar.gz'
subprocess.run(['scp', '-P', '25023', 'root@ssh5.vast.ai:' + manifest['path'], str(archive)], check=True)
with archive.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha256').hexdigest() == manifest['sha256']
with tarfile.open(archive) as stream:
    stream.extractall(root, filter='data')
(root / 'download_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
shutil.copyfile(__file__, root / 'download_results.py')
print('VERIFIED_RESULTS_DOWNLOAD', json.dumps(manifest))
