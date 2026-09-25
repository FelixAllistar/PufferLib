"""Primitive replay rewards share the production macro-step implementation."""
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.parametrize("sanitize", [False, True])
def test_primitive_application_matches_training(tmp_path, sanitize):
    outputs = []
    flags = ["-O1", "-g", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer"] if sanitize else ["-O2"]
    for primitive in [False, True]:
        binary = tmp_path / f"reward_{primitive}"
        subprocess.run(["cc", "-std=c11", *flags,
            *(["-DREPLAY_API"] if primitive else []),
            "-Isrc", "-Iocean/kaggriculture", "-Iraylib-5.5_linux_amd64/include",
            str(Path(__file__).with_suffix(".c")), "-lm", "-o", str(binary)],
            cwd=ROOT, check=True, capture_output=True)
        result = subprocess.run([str(binary)], cwd=ROOT, check=True,
            capture_output=True, text=True, timeout=60)
        assert "steps=2048 terminals=64" in result.stdout
        outputs.append(result.stdout)
    assert outputs[0] == outputs[1]
