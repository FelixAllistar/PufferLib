import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_suffix(".c")


@pytest.fixture(params=[False, True], ids=["optimized", "sanitized"])
def build(request, tmp_path):
    compiler = shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("C compiler unavailable")
    includes = next(ROOT.glob("raylib-*/include"), None)
    if includes is None:
        pytest.skip("run build.sh first to obtain raylib headers")
    flags = ["-O2"]
    if request.param:
        flags = ["-O1", "-g", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer"]

    def compile_at(root, name):
        executable = tmp_path / name
        header = root / "ocean/trianglepath/trianglepath.h"
        subprocess.run([
            compiler, "-std=c11", *flags, f"-I{root / 'src'}", f"-I{includes}",
            f'-DENV_HEADER="{header}"', str(SOURCE), "-lm", "-o", str(executable),
        ], check=True, cwd=ROOT, capture_output=True)
        return executable

    return compile_at


def test_oracle_and_cpu_adapter(build):
    executable = build(ROOT, "trianglepath")
    subprocess.run([executable], check=True, capture_output=True, timeout=30)


def test_legacy_trace_parity(build):
    reference = os.environ.get("TRIANGLEPATH_REFERENCE_ROOT")
    if reference is None:
        pytest.skip("optional: set TRIANGLEPATH_REFERENCE_ROOT to the old checkout")
    old = build(Path(reference).resolve(), "legacy")
    new = build(ROOT, "ported")
    expected = subprocess.run([old, "--trace"], check=True, capture_output=True, timeout=30)
    actual = subprocess.run([new, "--trace"], check=True, capture_output=True, timeout=30)
    assert actual.stdout == expected.stdout
