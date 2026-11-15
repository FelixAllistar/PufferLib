"""Bake the Briar Court exterior PBR texture library (deterministic).

Run:
  blender -b --factory-startup --python-exit-code 1 \
    -P ocean/swat/tools/blender/motel_exterior_textures.py -- SRC_DIR OUT_DIR

Inputs (SRC_DIR): the CC0 Poly Haven maps listed in SOURCES below, named
  <asset>_<Diffuse|nor_gl|Rough>_<res>.png. Missing files are downloaded from
  the recorded dl.polyhaven.org URLs and verified against the recorded MD5.
Outputs (OUT_DIR): 8-bit PNG maps consumed by motel_exterior_kit.py:
  <name>_basecolor.png (sRGB), <name>_normal.png (OpenGL +Y), <name>_orm.png
  (glTF metallicRoughness: R=1, G=roughness, B=metalness).

Each source is cropped to the physical size of one motel_v1 UV repeat
(~0.9 m; the original modules use ~1.1 UV/m), made seamless with a
half-period cross-fade, resampled to its budget, and tinted so the mean
colour matches the original Briar Court palette (the 192 px placeholder maps
embedded in motel_v1). Only numpy and Blender image IO are used.
"""
import bpy, sys, os, hashlib, urllib.request
import numpy as np

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
SRC, OUT = argv[0], argv[1]
os.makedirs(SRC, exist_ok=True); os.makedirs(OUT, exist_ok=True)

PH = 'https://dl.polyhaven.org/file/ph-assets/Textures/png/{res}/{a}/{a}_{m}_{res}.png'
MAPNAME = {'Diffuse': 'diff', 'nor_gl': 'nor_gl', 'Rough': 'rough'}
# asset id: (resolution, physical width in metres from the Poly Haven API, md5 per map)
SOURCES = {
    'beige_wall_002': ('4k', 3.0, {'Diffuse': None, 'nor_gl': None, 'Rough': None}),
    'concrete_floor_02': ('2k', 2.0, {'Diffuse': 'e6328911fe7cd3a74f11486f06a5ad60', 'nor_gl': 'e7bbae9a0ec85c590b82ccdf5788c46d', 'Rough': '130e55c56d8f1db1c1ed0cd7d160fa78'}),
    'painted_plaster_wall': ('2k', 2.0, {'Diffuse': 'a59f31e4461b4e9385e88030e32280d9', 'nor_gl': '2eba0da17c1d194537c1231cb412e310', 'Rough': 'ca7f611c7ccbc7604fab09c698a4d329'}),
    'green_metal_rust': ('2k', 1.0, {'Diffuse': 'd6b7e4ba43193c1a526bd943f52dc11c', 'nor_gl': '7e6eb0c41507137b07c84a4f8956dcb4', 'Rough': '55a52167b516c4ae75af5c48b50d7132'}),
}
UV_METRES = 1.0 / 1.1  # one motel_v1 UV unit

def fetch(asset, m):
    res = SOURCES[asset][0]
    path = os.path.join(SRC, f'{asset}_{m}_{res}.png')
    if not os.path.exists(path):
        urllib.request.urlretrieve(PH.format(res=res, a=asset, m=MAPNAME[m]), path)
    want = SOURCES[asset][2][m]
    if want and hashlib.md5(open(path, 'rb').read()).hexdigest() != want:
        raise SystemExit(f'md5 mismatch for {path}')
    return path

def load(path):
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = 'Non-Color'  # raw encoded values
    w, h = img.size
    a = np.empty(w * h * 4, np.float32); img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)[::-1, :, :3].copy()  # top row first

def save(a, path):
    h, w = a.shape[:2]
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=False, float_buffer=False)
    img.colorspace_settings.name = 'Non-Color'
    rgba = np.ones((h, w, 4), np.float32); rgba[..., :3] = np.clip(a, 0, 1)
    img.pixels.foreach_set(rgba[::-1].ravel())
    img.filepath_raw = path; img.file_format = 'PNG'
    bpy.context.scene.render.image_settings.compression = 100
    img.save(); bpy.data.images.remove(img)

def box_down(a, k):
    h, w = (a.shape[0] // k) * k, (a.shape[1] // k) * k
    return a[:h, :w].reshape(h // k, k, w // k, k, -1).mean((1, 3))

def resize(a, n):
    k = max(1, a.shape[0] // (2 * n))
    if k > 1: a = box_down(a, k)
    s = a.shape[0]
    x = (np.arange(n) + .5) * s / n - .5
    i0 = np.clip(np.floor(x).astype(int), 0, s - 1); i1 = np.clip(i0 + 1, 0, s - 1); f = (x - i0)[:, None]
    rows = a[i0] * (1 - f[..., None]) + a[i1] * f[..., None]
    cols = rows[:, i0] * (1 - f[None, :, :]) + rows[:, i1] * f[None, :, :]
    return cols

def seamless(a):
    n = a.shape[0]
    t = np.minimum(np.arange(n), n - 1 - np.arange(n)) / n  # 0 at the edge, .5 centre
    w1 = np.clip((t - .08) / .22, 0, 1); w1 = w1 * w1 * (3 - 2 * w1)
    w = (w1[:, None] * w1[None, :])[..., None]
    rolled = np.roll(np.roll(a, n // 2, 0), n // 2, 1)
    return a * w + rolled * (1 - w)

def crop(asset, m):
    a = load(fetch(asset, m))
    frac = min(1.0, UV_METRES / SOURCES[asset][1])
    c = int(round(a.shape[0] * frac))
    return a[:c, :c]

def srgb_to_lin(c): return np.where(c <= .04045, c / 12.92, ((c + .055) / 1.055) ** 2.4)
def lin_to_srgb(c): c = np.clip(c, 0, 1); return np.where(c <= .0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - .055)

def blur(a, r):
    # Wrapped box blur (twice): removes cross-fade/low-frequency blotches.
    for _ in range(2):
        for ax in (0, 1):
            c = np.cumsum(np.concatenate([a.take(range(-r - 1, 0), ax), a, a.take(range(0, r), ax)], ax), ax)
            n = a.shape[ax]
            a = (c.take(range(2 * r + 1, 2 * r + 1 + n), ax) - c.take(range(0, n), ax)) / (2 * r + 1)
    return a

def tint(diff, target_srgb, contrast=1.0, keep_chroma=.25, flatten=0.0):
    lin = srgb_to_lin(diff)
    lum = lin @ np.array([.2126, .7152, .0722], np.float32)
    if flatten: lum = lum / (blur(lum, lum.shape[0] // 8) / lum.mean()) ** flatten
    detail = (lum / lum.mean()) ** contrast
    chroma = lin / np.maximum(lin.mean((0, 1)), 1e-4)  # source colour variation
    chroma = chroma / np.maximum(chroma @ np.array([.2126, .7152, .0722], np.float32), 1e-4)[..., None]
    chroma = 1 + keep_chroma * (chroma - 1)
    t = srgb_to_lin(np.array(target_srgb, np.float32) / 255)
    out = t * detail[..., None] * chroma
    out = out * (t / np.maximum(out.mean((0, 1)), 1e-5))  # exact mean
    return lin_to_srgb(out)

def normal_out(a):
    v = a * 2 - 1; v /= np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-6)
    return v * .5 + .5

def bake(name, asset, size, colours, metal=0.0, rough_scale=1.0, rough_bias=0.0, flatten=0.0):
    d = seamless(crop(asset, 'Diffuse'))
    for cname, (rgb, ct, csize) in colours.items():
        save(tint(resize(d, csize or size), rgb, ct, flatten=flatten), os.path.join(OUT, f'{cname}_basecolor.png'))
    nrm = normal_out(resize(seamless(crop(asset, 'nor_gl')), size)); save(nrm, os.path.join(OUT, f'{name}_normal.png'))
    r = resize(seamless(crop(asset, 'Rough')), size)[..., 0]
    orm = np.stack([np.ones_like(r), np.clip(r * rough_scale + rough_bias, .05, 1), np.full_like(r, metal)], -1)
    save(orm, os.path.join(OUT, f'{name}_orm.png'))
    print('baked', name, size, list(colours))

# Palette: means of the original motel_v1 maps, slightly weathered/darkened.
# colour name: (target sRGB mean, detail contrast, basecolor size or None for the set size)
bake('stucco', 'beige_wall_002', 1024, {'stucco': ((150, 138, 106), 1.6, None), 'repair': ((176, 168, 142), 1.2, 512)}, rough_bias=.05, flatten=.8)
bake('concrete', 'concrete_floor_02', 1024, {'concrete': ((104, 106, 100), .8, None), 'concrete_dark': ((62, 66, 64), .8, None), 'roof': ((60, 62, 60), 1.0, 512)}, flatten=.3)
bake('plaster', 'painted_plaster_wall', 512, {'cream': ((188, 182, 152), 1.4, None)}, rough_scale=.9, flatten=.6)
bake('paint', 'green_metal_rust', 512, {'teal': ((30, 70, 66), 1.0, None), 'ochre': ((160, 100, 38), 1.0, None)}, rough_scale=.85)
