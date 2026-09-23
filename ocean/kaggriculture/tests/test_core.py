import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_suffix(".c")
HEADER = ROOT / "ocean/kaggriculture/core.h"


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
