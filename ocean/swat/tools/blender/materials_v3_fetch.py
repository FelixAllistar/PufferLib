#!/usr/bin/env python3
"""Download and verify the CC0 source scans for materials_v3 (stdlib only).

    python3 ocean/swat/tools/blender/materials_v3_fetch.py CACHE_DIR [--lock]

Reads materials_v3_recipe.json beside this script. Poly Haven maps (2K PNG
diffuse / OpenGL normal / roughness) come from api.polyhaven.com with their
published MD5; ambientCG 2K-PNG zips come from ambientcg.com and are checked
against the SHA-256 recorded in materials_v3_sources.lock.json. --lock
(re)writes that lock file from what was downloaded. Both sites are CC0 1.0.
Downloads are never committed; only the derived maps in
assets/environment/materials_v3/ are.
"""
import hashlib, io, json, os, sys, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
RECIPE = os.path.join(HERE, 'materials_v3_recipe.json')
LOCK = os.path.join(HERE, 'materials_v3_sources.lock.json')
UA = {'User-Agent': 'swat-materials-v3-fetch/1.0'}


def get(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def digest(data, kind):
    return hashlib.new(kind, data).hexdigest()


def fetch_polyhaven(asset, cache, lock):
    files = json.loads(get('https://api.polyhaven.com/files/' + asset))
    info = json.loads(get('https://api.polyhaven.com/info/' + asset))
    out = {}
    for key, channel in (('Diffuse', 'color'), ('nor_gl', 'normal'), ('Rough', 'roughness')):
        f = files[key]['2k']['png']
        path = os.path.join(cache, asset, os.path.basename(f['url']))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if not os.path.exists(path) or digest(open(path, 'rb').read(), 'md5') != f['md5']:
            data = get(f['url'])
            if digest(data, 'md5') != f['md5']:
                raise SystemExit('md5 mismatch ' + f['url'])
            open(path, 'wb').write(data)
        out[channel] = {'file': path, 'url': f['url'], 'md5': f['md5']}
    authors = ', '.join(sorted(info.get('authors', {})))
    lock[asset] = {'site': 'polyhaven', 'page': 'https://polyhaven.com/a/' + asset, 'license': 'CC0-1.0',
                   'authors': authors, 'dimensions_mm': info.get('dimensions'),
                   'maps': {k: {'url': v['url'], 'md5': v['md5']} for k, v in out.items()}}
    return out


def fetch_ambientcg(asset, cache, lock, relock):
    url = 'https://ambientcg.com/get?file=%s_2K-PNG.zip' % asset
    path = os.path.join(cache, asset, asset + '_2K-PNG.zip')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    want = lock.get(asset, {}).get('sha256')
    if not os.path.exists(path) or (want and digest(open(path, 'rb').read(), 'sha256') != want):
        open(path, 'wb').write(get(url))
    data = open(path, 'rb').read()
    sha = digest(data, 'sha256')
    if want and sha != want and not relock:
        raise SystemExit('sha256 mismatch ' + url)
    z = zipfile.ZipFile(io.BytesIO(data))
    out = {}
    for suffix, channel in (('_Color.png', 'color'), ('_NormalGL.png', 'normal'), ('_Roughness.png', 'roughness')):
        name = [n for n in z.namelist() if n.endswith(suffix)][0]
        target = os.path.join(cache, asset, name)
        if not os.path.exists(target):
            open(target, 'wb').write(z.read(name))
        out[channel] = {'file': target}
    lock[asset] = {'site': 'ambientcg', 'page': 'https://ambientcg.com/a/' + asset, 'license': 'CC0-1.0',
                   'authors': 'Lennart Demes (ambientCG)', 'zip': url, 'sha256': sha}
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cache = os.path.abspath(sys.argv[1]); relock = '--lock' in sys.argv
    recipe = json.load(open(RECIPE))
    lock = json.load(open(LOCK)) if os.path.exists(LOCK) else {}
    sources = {}
    for name, m in recipe['materials'].items():
        site, asset = m['source'].split(':')
        if asset in sources:
            continue
        sources[asset] = fetch_polyhaven(asset, cache, lock) if site == 'polyhaven' else fetch_ambientcg(asset, cache, lock, relock)
        print('ok', site, asset)
    json.dump({a: {c: v['file'] for c, v in s.items()} for a, s in sources.items()},
              open(os.path.join(cache, 'sources.json'), 'w'), indent=1, sort_keys=True)
    if relock or not os.path.exists(LOCK):
        used = {a: lock[a] for a in sorted(sources)}
        json.dump(used, open(LOCK, 'w'), indent=1, sort_keys=True)
        print('wrote', LOCK)


if __name__ == '__main__':
    main()
