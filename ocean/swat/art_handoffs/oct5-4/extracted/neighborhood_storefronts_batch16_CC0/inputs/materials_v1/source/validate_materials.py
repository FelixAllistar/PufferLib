#!/usr/bin/env python3
"""Validate maps, metric normals, source hashes, wrap continuity and rebuild.

No engine or Blender required. --rebuild makes an isolated fresh export and
compares all shipping PNGs and manifest byte for byte. --report writes QA JSON.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EXPECTED = {"plaster_painted": [1., 1.], "plaster_worn": [2., 2.], "floor_pine": [2., 2.],
            "framing_pine": [.4, 2.], "door_paint": [.6, 2.], "door_wood": [.6, 2.]}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(root):
    manifest = json.loads((root / "material_manifest.json").read_text())
    assert manifest["schema"] == "swat_environment_material_family_v1"
    assert manifest["status"] == "asset-only candidates; no live loader selection"
    assert manifest["license"] == "CC0-1.0"
    assert {m["id"] for m in manifest["materials"]} == set(EXPECTED)
    assert len(manifest["materials"]) == 6
    for name, expected in manifest["source_files"].items():
        assert not Path(name).is_absolute() and ".." not in Path(name).parts
        assert sha(root / name) == expected, f"Bad source hash: {name}"
    results, files, total_bytes, base_bytes = {}, set(), 0, 0
    for material in manifest["materials"]:
        mid = material["id"]
        assert material["tile_metres"] == EXPECTED[mid]
        assert material["resolution"] == [512, 512]
        assert material["metallic"] == 0.
        assert set(material["maps"]) == {"basecolor", "normal", "roughness"}
        results[mid] = {}
        for channel, record in material["maps"].items():
            filename = f"{mid}_{channel}.png"
            assert record["file"] == filename and filename not in files
            files.add(filename)
            path = root / filename
            assert sha(path) == record["sha256"], f"Bad image hash: {filename}"
            assert path.stat().st_size == record["bytes"]
            total_bytes += path.stat().st_size
            base_bytes += path.stat().st_size if channel == "basecolor" else 0
            image = Image.open(path)
            image.load()  # Close PNG's file handle before any contract assertion.
            assert image.size == (512, 512) and image.format == "PNG"
            assert image.mode == ("L" if channel == "roughness" else "RGB")
            assert record["color_space"] == ("sRGB" if channel == "basecolor" else "linear")
            a = np.asarray(image, dtype=float)
            assert np.isfinite(a).all()
            seams = []
            for axis in (1, 0):
                step = np.abs(np.diff(a, axis=axis)).mean()
                wrap = np.abs(np.take(a, 0, axis=axis) - np.take(a, -1, axis=axis)).mean()
                # First/last texel centers are one texel apart, not duplicates.
                # Permit near-flat quantized maps without division instability.
                assert wrap <= max(.35, step * 3), f"Large seam: {filename} axis {axis}"
                seams.append({"wrap_mean_byte_difference": float(wrap), "interior_mean_byte_difference": float(step), "ratio": float(wrap/max(step, 1e-9))})
            stats = {"mode": image.mode, "min": float(a.min()), "max": float(a.max()), "seam_u": seams[0], "seam_v": seams[1]}
            if channel == "normal":
                n = a / 127.5 - 1
                length = np.linalg.norm(n, axis=-1)
                assert np.max(np.abs(length - 1)) < .008
                assert n[..., 2].min() > .2
                stats["maximum_unit_length_error"] = float(np.max(np.abs(length - 1)))
            if channel == "roughness":
                assert a.min() >= 89 and a.max() <= 253
            results[mid][channel] = stats
        if mid == "plaster_worn":
            assert .001 < material["wear_area_fraction"] < .02
    assert files == {p.name for p in root.glob("*.png")}, "Unexpected/missing runtime PNG"
    budget = manifest["runtime_budget"]
    assert budget["png_count"] == 18
    assert budget["compressed_bytes_all_maps"] == total_bytes < 2_500_000
    assert budget["compressed_bytes_basecolor_only"] == base_bytes < 1_100_000
    assert budget["decoded_bytes_native_formats"] == 6*512*512*7
    assert budget["decoded_bytes_rgba8_all_maps"] == 18*512*512*4
    assert budget["estimated_rgba8_with_full_mips_bytes"] == 18*sum((512//2**i)**2 for i in range(10))*4
    assert budget["current_game_extra_loaded_bytes"] == 0
    return {"materials": results, "runtime_budget": budget}


def test_normal_convention(root):
    spec = importlib.util.spec_from_file_location("builder", ROOT / "source/build_materials.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    u, row = np.meshgrid((np.arange(512)+.5)/512, (np.arange(512)+.5)/512)
    v = 1 - row
    height = .001 * (np.sin(2*np.pi*u) + np.sin(2*np.pi*v))
    decoded = builder.normal_from_height(height, [.4, 2.]) * 2 - 1
    expected = np.stack((-.001*2*np.pi/.4*np.cos(2*np.pi*u), -.001*2*np.pi/2*np.cos(2*np.pi*v), np.ones_like(u)), axis=-1)
    expected /= np.linalg.norm(expected, axis=-1, keepdims=True)
    assert np.max(np.abs(decoded-expected)) < 1e-6, "Metric normal handedness/scale regression"
    # Independent pixel/basis transform tests the actual shipped floor normal,
    # not a restatement of the intended sign convention on constants.
    src = Image.open(root / "source/cc0/wood_floor_worn_nor_gl_1k.jpg").convert("RGB")
    src = src.resize((512, 512), Image.Resampling.LANCZOS).transpose(Image.Transpose.ROTATE_90)
    n = np.asarray(src, dtype=float) / 127.5 - 1
    transformed = np.stack((-.65*n[..., 1], .65*n[..., 0], n[..., 2]), axis=-1)
    transformed /= np.linalg.norm(transformed, axis=-1, keepdims=True)
    expected_bytes = np.rint((transformed*.5+.5)*255).astype(np.uint8)
    actual = np.asarray(Image.open(root / "floor_pine_normal.png"))
    assert np.array_equal(actual, expected_bytes), "Source scan normal pixel/basis rotation regression"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    report = check(root)
    test_normal_convention(root)
    report["metric_normal_convention_test"] = "passed, +U/+V slope signs and rectangular tile spacing"
    report["independent_rebuild"] = "not run"
    if args.rebuild:
        with tempfile.TemporaryDirectory(prefix="swat-materials-rebuild-") as td:
            subprocess.run([sys.executable, str(root / "source/build_materials.py"), "--out", td], check=True)
            for path in list(root.glob("*.png")) + [root / "material_manifest.json"]:
                assert path.read_bytes() == (Path(td)/path.name).read_bytes(), f"Non-identical rebuild: {path.name}"
        report["independent_rebuild"] = "all 18 PNGs and manifest byte-identical"
    report["result"] = "passed"
    report["normal_quantization_note"] = "RGB8 normals are normalized by consumer after decoding/filtering. Seam checks compare neighboring texel centers, not identical edge pixels."
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({k:v for k,v in report.items() if k != "materials"}, indent=2))


if __name__ == "__main__":
    main()
