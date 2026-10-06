#!/usr/bin/env python3
"""CC0 material family. Original procedural walls/wood/paint and CC0 scan floor.

Reference: CPython 3.12, NumPy 2.3.5, Pillow 12.3.0. Deterministic seeded,
periodic fields; lighting is never evaluated during basecolor generation.
Run from anywhere: python build_materials.py [--out /tmp/material-rebuild].
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform

import numpy as np
from PIL import Image, __version__ as pillow_version

SOURCE = Path(__file__).resolve().parent
DEFAULT_OUT = SOURCE.parent
CHANNELS = {"basecolor": ("RGB", "sRGB"), "normal": ("RGB", "linear"), "roughness": ("L", "linear")}
CC0_INPUTS = {
    "wood_floor_worn_diff_1k.jpg": "db386853009b92b9edd7255b35c9f7b0d4b7de16837a7a9da8422148754bc758",
    "wood_floor_worn_nor_gl_1k.jpg": "f428496e5d6c726d17d9ecea29c0acd8660348630acf791cea7bdd7f3c6e262b",
    "wood_floor_worn_rough_1k.jpg": "9d7492db1197f1386466dfd7dd43f692446166c1433ddeebf66c10e8b226a6d0",
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def smooth(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def noise(u, v, nx, ny, rng):
    """Quintic periodic value noise. Evaluate on a torus, never pad edges."""
    grid = rng.uniform(-1.0, 1.0, (ny, nx))
    x, y = (u % 1) * nx, (v % 1) * ny
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    sx, sy = smooth(x - ix), smooth(y - iy)
    a, b = grid[iy % ny, ix % nx], grid[iy % ny, (ix + 1) % nx]
    c, d = grid[(iy + 1) % ny, ix % nx], grid[(iy + 1) % ny, (ix + 1) % nx]
    return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy


def fbm(u, v, rng, sizes=(4, 9, 21, 48, 110), amplitudes=(.32, .25, .20, .14, .09)):
    return sum(a * noise(u, v, n, n, rng) for a, n in zip(amplitudes, sizes))


def torus_delta(a, center):
    return (a - center + .5) % 1 - .5


def normal_from_height(height, tile_metres):
    """Rows go down; +V goes up. Tangent OpenGL normal = (-dh/du,-dh/dv,1)."""
    n = height.shape[0]
    du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) / (2 * tile_metres[0] / n)
    dv = (np.roll(height, 1, axis=0) - np.roll(height, -1, axis=0)) / (2 * tile_metres[1] / n)
    normal = np.stack((-du, -dv, np.ones_like(height)), axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return normal * .5 + .5


def wood_fields(u, v, rng, kind):
    # Growth-ring cross section distorted along the tree, not a lit cylinder.
    warp = noise(u, v, 3, 5, rng) * .043 + noise(u, v, 7, 11, rng) * .012
    warp += .011 * np.sin(2 * np.pi * (v * 2 + noise(u, v, 2, 3, rng)))
    rings = 21 if kind == "framing_pine" else 29
    phase = (u + warp) * rings
    # A little elliptical deviation where a branch crossed the board.
    for cx, cy, radius in ((.32, .28, .045), (.77, .79, .037)):
        dx, dy = torus_delta(u, cx), torus_delta(v, cy)
        influence = np.exp(-((dx / radius) ** 2 + (dy / .10) ** 2) * 1.3)
        phase += influence * np.sin(np.arctan2(dy * .3, dx)) * 1.35
    growth = np.sin(2 * np.pi * phase)
    latewood = np.clip((growth - .37) / .63, 0, 1) ** 2
    grain = noise((u + warp) % 1, v, 170, 11, rng)
    rays = noise(u, v, 105, 89, rng)
    broad = noise(u, v, 6, 7, rng)
    # Unequal pigment bands broken by elongated fibres. Keep rings subordinate
    # to natural fibre variation instead of making uniform zebra stripes.
    fibre = noise((u + warp * .42) % 1, v, 240, 18, rng)
    broken = .35 + .65 * (noise(u, v, 11, 17, rng) * .5 + .5)
    value = -.023 * latewood * broken + .055 * grain + .032 * broad + .014 * rays + .014 * fibre
    relief = -.32 * latewood * broken + .37 * grain + .14 * rays + .17 * fibre
    return value, relief, latewood


def generate(p, resolution):
    rng = np.random.Generator(np.random.PCG64(p["seed"]))
    # Pixel centres, not duplicated endpoints. PNG top row corresponds to V=1.
    u, row = np.meshgrid((np.arange(resolution) + .5) / resolution, (np.arange(resolution) + .5) / resolution)
    v = 1 - row
    kind = p["id"]
    base = np.broadcast_to(np.array(p["pigment_srgb"], dtype=float), (resolution, resolution, 3)).copy()
    roughness = np.full((resolution, resolution), p["roughness"], dtype=float)
    extra = {}

    if kind.startswith("plaster"):
        macro = fbm(u, v, rng)
        fine = noise(u, v, 145, 151, rng)
        pores = np.clip((-fine - .32) / .68, 0, 1) ** 2
        # Pigment, aggregate and chalk differences only; no height-derived shading.
        variation = .009 * macro + .004 * fine - .005 * pores
        height = p["height_scale_m"] * (.34 * macro + .47 * fine - .55 * pores)
        roughness += .022 * macro + .035 * pores
        if kind == "plaster_worn":
            # Four small scattered abraded chalk islands over 4 m², not brown stains.
            mask = np.zeros_like(u)
            edge = noise(u, v, 69, 73, rng)
            for cx, cy, sx, sy in ((.19, .26, .052, .080), (.63, .72, .032, .045), (.80, .21, .019, .036), (.45, .47, .022, .030)):
                d = (torus_delta(u, cx) / sx) ** 2 + (torus_delta(v, cy) / sy) ** 2
                # Broken, irregular chalk flecks within a local abrasion zone.
                # Avoid filled circular repair blobs that advertise the repeat.
                patch = np.clip((.70 - d + edge * 1.25) * 2.8, 0, 1)
                patch *= np.clip((edge + .18) * 2.6, 0, 1)
                mask = np.maximum(mask, patch)
            # Bright pale mineral substrate: no ambient occlusion, shadow or grime.
            variation += .017 * mask
            height -= .00025 * mask
            roughness += .045 * mask
            extra["wear_area_fraction"] = float(np.mean(mask > .1))
            extra["wear_note"] = "Shallow cosmetic chalk abrasions; use on selected pieces only. Never repeat this variant on all walls. No structural damage or transparency."
        base += variation[..., None]

    elif kind == "floor_pine":
        for filename, expected in CC0_INPUTS.items():
            assert sha(SOURCE / "cc0" / filename) == expected, f"Source changed: {filename}"
        # Rotate the full 2m scan 90 degrees: grain goes from U to V. The
        # tangent normal X/Y components must rotate too, not just its pixels.
        src = SOURCE / "cc0"
        albedo = np.asarray(Image.open(src / "wood_floor_worn_diff_1k.jpg").convert("RGB").resize((resolution, resolution), Image.Resampling.LANCZOS), dtype=float) / 255
        albedo = np.rot90(albedo)
        # This is pigment grading only, never deriving diffuse from normals.
        luminance = albedo @ np.array([.2126, .7152, .0722])
        chroma = albedo - luminance[..., None]
        base = np.clip(np.array(p["pigment_srgb"]) + .63 * (luminance - luminance.mean())[..., None] + .18 * (chroma - chroma.mean(axis=(0, 1))), .025, .9)
        n = np.asarray(Image.open(src / "wood_floor_worn_nor_gl_1k.jpg").convert("RGB").resize((resolution, resolution), Image.Resampling.LANCZOS), dtype=float) / 127.5 - 1
        n = np.rot90(n)
        nx = -n[..., 1].copy()
        ny = n[..., 0].copy()
        n[..., 0], n[..., 1] = nx * .65, ny * .65
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        roughness = np.asarray(Image.open(src / "wood_floor_worn_rough_1k.jpg").convert("L").resize((resolution, resolution), Image.Resampling.LANCZOS), dtype=float) / 255
        roughness = np.clip(.47 + .38 * np.rot90(roughness), .47, .85)
        extra.update(source="Poly Haven wood_floor_worn, Dimitrios Savva, CC0-1.0", source_normal_strength=.65,
                     transformation="512px Lanczos; rotate pixels 90 degrees CCW; rotate tangent normals (Nx,Ny)->(-Ny,Nx); reduce tangent slopes 0.65 and renormalize; desaturate/regrade diffuse; roughness=0.47+0.38*source.",
                     roughness_range=[float(roughness.min()), float(roughness.max())],
                     height_range_metres=None)
        return {"basecolor": base, "normal": n * .5 + .5, "roughness": roughness}, extra


    else:
        value, relief, latewood = wood_fields(u, v, rng, kind)
        if kind == "door_paint":
            # Opaque paint mutes grain; slight brush texture is geometric only.
            pigment = .0030 * noise(u, v, 13, 15, rng) + .002 * noise(u, v, 112, 21, rng)
            base += pigment[..., None]
            height = p["height_scale_m"] * (relief * .24 + .45 * noise(u, v, 149, 13, rng))
            roughness += .018 * noise(u, v, 45, 47, rng)
        else:
            base += value[..., None] * np.array([1.0, .85, .64])
            height = p["height_scale_m"] * relief
            roughness += .035 * latewood + .028 * noise(u, v, 27, 31, rng)
    result = {"basecolor": np.clip(base, .015, .95), "normal": normal_from_height(height, p["tile_metres"]), "roughness": np.clip(roughness, .35, .99)}
    extra["height_range_metres"] = [float(height.min()), float(height.max())]
    extra["roughness_range"] = [float(result["roughness"].min()), float(result["roughness"].max())]
    return result, extra


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--parameters", type=Path, default=SOURCE / "parameters.json")
    args = parser.parse_args()
    params = json.loads(args.parameters.read_text())
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    n = params["resolution"]
    assert n == 512, "This version's shipping contract is six 512px materials."
    records = []
    for p in params["materials"]:
        channels, extra = generate(p, n)
        maps = {}
        for channel, pixels in channels.items():
            name = f"{p['id']}_{channel}.png"
            encoded = np.rint(pixels * 255).astype(np.uint8)
            Image.fromarray(encoded).save(out / name, format="PNG", optimize=False, compress_level=9)
            maps[channel] = {"file": name, "sha256": sha(out / name), "bytes": (out / name).stat().st_size,
                             "mode": CHANNELS[channel][0], "color_space": CHANNELS[channel][1]}
        records.append({**p, **extra, "resolution": [n, n], "maps": maps, "metallic": 0.0,
                        "grain_axis": "+V" if "plaster" not in p["id"] else None,
                        "normal_application_strength": 1.0,
                        "roughness_encoded_range": [float(np.rint(channels["roughness"].min()*255)/255), float(np.rint(channels["roughness"].max()*255)/255)]})
    runtime = sum(m["bytes"] for p in records for m in p["maps"].values())
    basecolor = sum(p["maps"]["basecolor"]["bytes"] for p in records)
    manifest = {
        "schema": "swat_environment_material_family_v1", "created_utc": params["created_utc"],
        "base_commit": "a2d84371c9cbadf46733afb87cc39b73f72dc405", "status": "asset-only candidates; no live loader selection",
        "license": "CC0-1.0", "source": "Five original deterministic procedural fields plus the verified Poly Haven CC0 wood_floor_worn diffuse/normal/roughness scan for floor_pine. No imagegen artwork is used or relabeled.",
        "source_files": {"source/build_materials.py": sha(SOURCE / "build_materials.py"), "source/parameters.json": sha(args.parameters)},
        "reference_build": {"python": platform.python_version(), "numpy": np.__version__, "pillow": pillow_version},
        "encoding": {"basecolor": "RGB8 sRGB; decode exactly once to linear before shading. Procedural surfaces contain no lighting/AO/specular. Scan floor is derived from official Diffuse, with no added shading; minor source cavity shading may remain and is not certified calibrated albedo.",
                     "normal": "RGB8 linear, tangent-space OpenGL +Y/+V. N=normalize(2*RGB-1), +U right, +V up, outward +Z. PNG row 0 corresponds to V=1. Do not sRGB-decode. DirectX consumers must invert green exactly once.",
                     "roughness": "R8 linear perceptual roughness in [0,1], not gloss. No sRGB decode. Microfacet alpha = roughness squared where required by shader.",
                     "sampler": "REPEAT U/V; trilinear mipmapping; normalize filtered normals. Non-square physical tiles still use square images.",
                     "height": "Procedural metres; normal derived using independent U/V physical texel spacing. Height is source-only and not a runtime displacement map."},
        "runtime_budget": {"png_count": 18, "compressed_bytes_all_maps": runtime, "compressed_bytes_basecolor_only": basecolor,
                           "decoded_bytes_native_formats": 6*n*n*7, "decoded_bytes_rgba8_all_maps": 18*n*n*4,
                           "estimated_rgba8_with_full_mips_bytes": 18*sum((n//(2**i))**2 for i in range(10))*4,
                           "current_game_extra_loaded_bytes": 0},
        "binding_advisory": {"plaster_painted": "SWAT_DRYWALL/SWAT_PLASTER intact skins; preserve each existing damage owner.",
                            "plaster_worn": "Opt-in sparse cosmetic variant on selected existing skins only; no per-cell randomized UV offsets.",
                            "floor_pine": "SWAT_WOOD floor role only, never all wooden boxes.",
                            "framing_pine": "SWAT_WOOD framing/support role; align V with long member axis.",
                            "door_paint": "Existing door leaf painted primitives; remap UVs to metres, retain same moving door owner.",
                            "door_wood": "Existing exposed timber/chip primitives on same door owner; no additional surviving overlay."},
        "limitations": ["Current engine selects one wood map and supports basecolor only. Role-specific map binding, rectangular tiles and tangent-space PBR need a separate engine-agent integration.",
                        "No loader, shared material enum, shader, lighting, geometry, gameplay, networking, damage, original texture path, or existing GLB is changed.",
                        "Existing door GLB UVs are not a metric contract; these maps are not drop-in embedded replacements.",
                        "Procedural surfaces have authored physical dimensions; floor uses the source scan's documented 2m tile. No instrument-measured BRDF values are claimed.",
                        "Floor source is an official Diffuse scan: visual inspection found no broad directional illumination gradient, but small dark knot/nail/joint features may contain residual cavity occlusion. No AO is added. This is not a certified shadow-free albedo measurement.",
                        "Abraded islands repeat every 2 m if tiled. Use worn plaster sparingly; broad damage/local masks remain engine-owned.",
                        "No end-grain, separate normal mipchain, BC compression, or distant LOD supplied.",
                        "CPU QA renders are reference material tests, not captures from the current SWAT renderer."],
        "materials": records}
    manifest["source_files"].update({"source/cc0/" + name: checksum for name, checksum in CC0_INPUTS.items()})
    manifest["external_source_provenance"] = {"asset": "wood_floor_worn", "author": "Dimitrios Savva", "license": "CC0-1.0",
        "asset_url": "https://polyhaven.com/a/wood_floor_worn", "license_url": "https://polyhaven.com/license", "license_verified_utc": "2026-10-04",
        "physical_width_m": 2.0, "source": "Unmodified local house_kit/textures source files; hashes matched source provenance, official asset and license pages freshly read 2026-10-04.",
        "files": {name: {"sha256": checksum, "download_url": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/wood_floor_worn/" + name} for name, checksum in CC0_INPUTS.items()}}
    (out / "material_manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(json.dumps(manifest["runtime_budget"], indent=2))


if __name__ == "__main__":
    main()
