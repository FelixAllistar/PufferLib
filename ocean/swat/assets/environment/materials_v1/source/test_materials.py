#!/usr/bin/env python3
"""Negative-fixture contract tests. All mutations stay inside a temporary copy."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("validator", ROOT / "source/validate_materials.py")
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


class MaterialContractTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="swat-material-fixture-")
        self.root = Path(self.temp.name) / "materials_v1"
        shutil.copytree(ROOT, self.root, ignore=shutil.ignore_patterns("qa", "__pycache__"))
        self.manifest = json.loads((self.root / "material_manifest.json").read_text())

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        (self.root / "material_manifest.json").write_text(json.dumps(self.manifest))

    def rejected(self):
        with self.assertRaises((AssertionError, ValueError, OSError)):
            validator.check(self.root)

    def test_baseline(self):
        validator.check(self.root)
        validator.test_normal_convention(self.root)

    def test_bad_source_hash(self):
        p = self.root / "source/parameters.json"
        p.write_bytes(p.read_bytes() + b" ")
        self.rejected()

    def test_wrong_color_space(self):
        self.manifest["materials"][0]["maps"]["normal"]["color_space"] = "sRGB"
        self.save()
        self.rejected()

    def test_wrong_metric_scale(self):
        self.manifest["materials"][0]["tile_metres"] = [2, 2]
        self.save()
        self.rejected()

    def test_path_escape(self):
        self.manifest["materials"][0]["maps"]["basecolor"]["file"] = "../plaster_diffuse.png"
        self.save()
        self.rejected()

    def test_unexpected_image(self):
        Image.new("RGB", (512, 512)).save(self.root / "unexpected.png")
        self.rejected()

    def test_invalid_normal_even_with_matching_hash(self):
        rec = self.manifest["materials"][0]["maps"]["normal"]
        path = self.root / rec["file"]
        Image.new("RGB", (512, 512), (0, 0, 0)).save(path)
        rec.update(sha256=hashlib.sha256(path.read_bytes()).hexdigest(), bytes=path.stat().st_size)
        self.save()
        self.rejected()

    def test_wrong_scan_normal_basis(self):
        path = self.root / "floor_pine_normal.png"
        im = Image.open(path).convert("RGB")
        r, g, b = im.split()
        Image.merge("RGB", (r, g.point(lambda x: 255 - x), b)).save(path)
        with self.assertRaises(AssertionError):
            validator.test_normal_convention(self.root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
