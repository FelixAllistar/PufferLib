from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.parametrize("paired", [False, True])
def test_terminal_wld_and_same_state_features(tmp_path, paired):
    output = tmp_path / "wld"
    subprocess.run(["cc", "-std=c11", "-O2", "-Isrc", "-Iocean/kaggriculture",
        "-Iraylib-5.5_linux_amd64/include", *(["-DKAG_WITH_PAIRED_CRITIC"] if paired else []),
        str(Path(__file__).with_suffix(".c")), "raylib-5.5_linux_amd64/lib/libraylib.a",
        "-lGL", "-lpthread", "-ldl", "-lm", "-o", str(output)], cwd=ROOT,
        check=True, capture_output=True)
    result = subprocess.run([str(output)], cwd=ROOT, check=True, capture_output=True, text=True)
    assert "derivatives PASS" in result.stdout
