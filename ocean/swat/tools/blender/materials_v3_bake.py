"""Bake the Briar Court materials_v3 maps from CC0 scans (Blender 4.2, numpy).

    blender -b --factory-startup -P ocean/swat/tools/blender/materials_v3_bake.py -- CACHE_DIR OUT_DIR [NAME ...]

CACHE_DIR comes from materials_v3_fetch.py. For every material in
materials_v3_recipe.json (or only the NAMEs given) this writes
OUT_DIR/<name>_basecolor.jpg (sRGB), <name>_normal.jpg (OpenGL +Y, linear)
and <name>_roughness.jpg (linear grey in RGB, so R and G both carry it). Steps, all wrap-around so the scans stay
seamless: integer area downsample to `res`; multiplicative low-frequency
flatten (removes blotches that read as repetition from a distance);
saturation/contrast; per-channel gain to the hill-climbed `target` mean
colour; optional two-colour recolour (vinyl tile) and baked expansion joints;
roughness scale/offset and normal strength. JPEG (quality 92/95/90) keeps
the whole set small; raylib's stb_image decodes it.
"""
import json, os, sys
import numpy as np
import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
RECIPE = os.path.join(HERE, 'materials_v3_recipe.json')


def load(path):
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = 'Non-Color'
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[::-1, :, :3].astype(np.float64)  # top row first


def downsample(a, res):
    h, w = a.shape[:2]
    f = h // res
    assert h == w and f >= 1 and f * res == h, (a.shape, res)
    if f == 1:
        return a
    return a.reshape(res, f, res, f, -1).mean(axis=(1, 3))


def blur(a, sigma):
    """Gaussian blur with wrap-around (FFT)."""
    h, w = a.shape[:2]
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))
    out = np.empty_like(a)
    for c in range(a.shape[2]):
        out[..., c] = np.real(np.fft.ifft2(np.fft.fft2(a[..., c]) * g))
    return out


LUM = np.array([0.2126, 0.7152, 0.0722])


def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def save_jpg(path, a, quality):
    """8-bit JPEG through Blender's writer (raw buffer, no view transform)."""
    a = np.clip(a, 0, 1)
    if a.shape[2] == 1:
        a = np.repeat(a, 3, axis=2)
    h, w = a.shape[:2]
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=False)
    img.colorspace_settings.name = 'Non-Color'
    rgba = np.ones((h, w, 4), np.float32); rgba[..., :3] = a[::-1]
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = path; img.file_format = 'JPEG'
    img.save(quality=quality)
    bpy.data.images.remove(img)


def joint_profile(n, width, axis_len):
    """Signed distance (px) to the nearest groove centre at 0 (wrap)."""
    x = np.arange(axis_len, dtype=np.float64)
    return np.minimum(x, axis_len - x) - 0.0


def bake(name, m, src, out):
    res = m['res']
    color = downsample(load(src['color']), res)
    normal = downsample(load(src['normal']), res)
    rough = downsample(load(src['roughness']), res)[..., :1]
    lin = srgb_to_lin(np.clip(color, 0, 1))
    # 1. Low-frequency flatten (multiplicative, keeps hue).
    if m.get('flatten', 0) > 0:
        low = blur(lin, res * 0.10) + 1e-4
        lin = lin * (low.mean(axis=(0, 1)) / low) ** m['flatten']
    # 2. Saturation and luminance contrast about the mean.
    lum = (lin @ LUM)[..., None]
    lin = lum + (lin - lum) * m.get('saturation', 1.0)
    mlum = lum.mean()
    if m.get('contrast', 1.0) != 1.0:
        lin = lin * (np.maximum(lum, 1e-5) / mlum) ** (m['contrast'] - 1.0)
    # 3. Optional two-colour recolour (vinyl composition tile from a stone checker).
    rc = m.get('recolor')
    if rc:
        lum = (lin @ LUM)[..., None]
        t = srgb_to_lin(np.float64(rc['threshold']))
        mask = np.clip((lum - t * 0.8) / (t * 0.4), 0, 1)
        detail = (lum / (blur(lum, res * 0.02) + 1e-4)) ** rc.get('detail', 0.6)
        dark = srgb_to_lin(np.array(rc['dark']) / 255.0); light = srgb_to_lin(np.array(rc['light']) / 255.0)
        lin = (dark * (1 - mask) + light * mask) * detail
    # 4. Gain to the target mean colour (linear).
    else:
        target = srgb_to_lin(np.array(m['target']) / 255.0)
        lin = lin * (target / lin.reshape(-1, 3).mean(axis=0))
    r = np.clip(rough * m['rough'][0] + m['rough'][1], 0.02, 1.0)
    n = normal * 2 - 1
    n[..., :2] *= m.get('normal', 1.0)
    # 5. Optional expansion/control joints at the texture edge (wrap).
    j = m.get('joints')
    if j:
        axes = j['axis']
        groove = np.zeros((res, res))
        slope_x = np.zeros((res, res)); slope_y = np.zeros((res, res))
        half = j['width_px'] / 2.0
        d = joint_profile(res, j['width_px'], res)
        prof = np.clip((half + 1.5 - d) / 1.5, 0, 1)             # 1 in the groove, ramps at its lips
        dprof = np.gradient(np.concatenate([prof[-2:], prof, prof[:2]]))[2:-2]
        if 'u' in axes:
            groove = np.maximum(groove, prof[None, :]); slope_x += dprof[None, :]
        if 'v' in axes:
            groove = np.maximum(groove, prof[:, None]); slope_y -= dprof[:, None]
        lin = lin * (1 - groove[..., None] * (1 - j['darken']))
        n[..., 0] += slope_x * j['depth']; n[..., 1] += slope_y * j['depth']
        r = np.maximum(r, groove[..., None] * 0.95)
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    save_jpg(os.path.join(out, name + '_basecolor.jpg'), lin_to_srgb(lin), 92)
    save_jpg(os.path.join(out, name + '_normal.jpg'), n * 0.5 + 0.5, 95)
    save_jpg(os.path.join(out, name + '_roughness.jpg'), r, 90)
    srgb = np.round(255 * lin_to_srgb(lin).reshape(-1, 3).mean(axis=0)).astype(int).tolist()
    print('baked', name, res, 'mean sRGB', srgb, 'rough %.2f' % float(r.mean()))


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    cache, out = argv[0], argv[1]
    only = set(argv[2:])
    recipe = json.load(open(RECIPE))
    sources = json.load(open(os.path.join(cache, 'sources.json')))
    os.makedirs(out, exist_ok=True)
    for name, m in recipe['materials'].items():
        if only and name not in only:
            continue
        bake(name, m, sources[m['source'].split(':')[1]], out)


main()
