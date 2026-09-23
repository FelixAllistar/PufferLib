import os
from pathlib import Path
import shutil
import subprocess

import pytest


SOURCE = Path(__file__).with_suffix(".c")


@pytest.fixture(scope="module", params=[False, True], ids=["optimized", "sanitized"])
def build(request, tmp_path_factory):
    compiler = shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("C compiler unavailable")
    directory = tmp_path_factory.mktemp("kag-policy")
    flags = ["-O2"]
    if request.param:
        flags = ["-O1", "-g", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer"]
    compiled = {}

    def compile_policy(reference=False):
        if reference in compiled:
            return compiled[reference]
        includes = []
        warnings = ["-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
        if reference:
            source = os.environ.get("KAGGRICULTURE_REFERENCE_ROOT")
            if source is None:
                pytest.skip("set KAGGRICULTURE_REFERENCE_ROOT to the preserved old checkout")
            old = Path(source).resolve()
            header = old / "ocean/kaggriculture/kaggriculture.h"
            includes = [f'-DKAG_REFERENCE="{header}"', f"-I{old / 'src'}",
                "-I/usr/local/include", "-mavx2", "-D_POSIX_C_SOURCE=200809L"]
            warnings = []  # Do not impose this port's style on the untouched reference.
        executable = directory / ("legacy" if reference else "ported")
        command = [compiler, "-std=c11", *flags, *warnings, *includes,
            "-ffunction-sections", "-fdata-sections", str(SOURCE),
            "-Wl,--gc-sections", "-lm", "-o", str(executable)]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        compiled[reference] = executable
        return executable

    return compile_policy


def test_controller_regressions(build):
    result = subprocess.run([build()], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize("mode", ["--trace", "--rich-trace"])
def test_controller_observation_and_sampling_parity(build, mode):
    old = subprocess.run([build(True), mode], capture_output=True, text=True, timeout=180)
    new = subprocess.run([build(), mode], capture_output=True, text=True, timeout=180)
    assert old.returncode == 0, old.stderr
    assert new.returncode == 0, new.stderr
    expected = old.stdout.splitlines()
    actual = new.stdout.splitlines()
    count = 12 * 719 if mode == "--trace" else 64 * 4 - 8
    assert len(expected) == len(actual) == count
    for index, (old_line, new_line) in enumerate(zip(expected, actual)):
        assert old_line == new_line, f"first mismatch at row {index}: {old_line} != {new_line}"
