import os
import ctypes
import hashlib
import importlib
from pathlib import Path
import random
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_suffix(".c")
HEADER = ROOT / "ocean/kaggriculture/core.h"


def test_official_market_prices(tmp_path):
    if os.environ.get("KAGGRICULTURE_OFFICIAL_PARITY") != "1":
        pytest.skip("set KAGGRICULTURE_OFFICIAL_PARITY=1 with kaggle-environments installed")
    official = importlib.import_module("kaggle_environments.envs.kaggriculture.kaggriculture")
    library = tmp_path / "core.so"
    subprocess.run(["cc", "-x", "c", "-O2", "-shared", "-fPIC", str(HEADER),
                    "-lm", "-o", str(library)], check=True, capture_output=True)
    native = ctypes.CDLL(str(library))
    native.kg_market_price.argtypes = [ctypes.c_int, ctypes.c_int]
    native.kg_market_price.restype = ctypes.c_int
    digest = hashlib.sha256(Path(official.__file__).read_bytes()).hexdigest()
    print(f"official environment source SHA256={digest}")
    for product, item in enumerate(official.PRODUCTS):
        for inventory in range(20001):
            expected = official.market_price(item, inventory)
            actual = native.kg_market_price(product, inventory)
            assert actual == expected, (item, inventory, expected, actual, digest)


@pytest.fixture(params=[False, True], ids=["optimized", "sanitized"])
def build(request, tmp_path):
    compiler = shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("C compiler unavailable")
    flags = ["-O2"]
    if request.param:
        flags = ["-O1", "-g", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer"]

    def compile_header(header, name):
        executable = tmp_path / name
        subprocess.run([
            compiler, "-std=c11", *flags, "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", f'-DCORE_HEADER="{header}"', str(SOURCE),
            "-lm", "-o", str(executable),
        ], check=True, capture_output=True)
        return executable

    return compile_header


def test_rule_regressions(build):
    executable = build(HEADER, "core")
    subprocess.run([executable], check=True, capture_output=True, timeout=30)


def test_python_random_stream(build):
    executable = build(HEADER, "core")
    result = subprocess.run([executable, "--rng"], check=True,
        capture_output=True, text=True, timeout=30)
    generators = {}
    for line in result.stdout.splitlines():
        seed, value = map(int, line.split())
        if seed not in generators:
            generators[seed] = random.Random(seed)
        assert value == generators[seed].getrandbits(32)


@pytest.mark.parametrize("mode", ["--trace", "--rule-trace"])
def test_legacy_state_and_reset_parity(build, mode):
    reference = os.environ.get("KAGGRICULTURE_REFERENCE_ROOT")
    if reference is None:
        pytest.skip("set KAGGRICULTURE_REFERENCE_ROOT to the preserved old checkout")
    old = build(Path(reference).resolve() / "ocean/kaggriculture/kaggriculture_core.c",
        "legacy")
    new = build(HEADER, "ported")
    expected = subprocess.run([old, mode], check=True,
        capture_output=True, timeout=120)
    actual = subprocess.run([new, mode], check=True,
        capture_output=True, timeout=120)
    expected_lines = expected.stdout.splitlines()
    actual_lines = actual.stdout.splitlines()
    assert len(expected_lines) == len(actual_lines) == 24 * 2 * (719 + 29)
    for index, (old_line, new_line) in enumerate(zip(expected_lines, actual_lines)):
        assert old_line == new_line, f"first state mismatch at trace row {index}"
