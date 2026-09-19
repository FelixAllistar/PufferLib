"""Headless, CPU-only viewer CLI regression; reads but never writes a checkpoint.

Run from the repository root with a built viewer and a compatible CNN checkpoint.
No display is opened, and no trainer process is started or signaled.
"""
import argparse
import os
from pathlib import Path
import subprocess


def check(binary, arguments, expected, success=True):
    result = subprocess.run(
        [str(binary), *map(str, arguments)], capture_output=True, text=True,
        env=dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", OMP_WAIT_POLICY="PASSIVE"),
        timeout=120,
    )
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == success, output
    for text in expected:
        assert text in output, output
    print(f"PASS: {' '.join(map(str, arguments))}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("checkpoint", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    checkpoint = args.checkpoint.resolve(strict=True)
    check(binary, ["watch", "--help"], ["--level LEVEL", "--single-life"])
    check(binary, ["watch", checkpoint, "--level", "4-2", "--inspect-check"],
          ["Playback: start 4-2; natural lives", "PASS: inspector checked 600 decisions"])
    check(binary, ["watch", checkpoint, "8-4", "--single-life", "--inspect-check"],
          ["Playback: start 8-4; single-life", "PASS: inspector checked 600 decisions"])
    check(binary, ["watch", checkpoint, "--level", "2-1"],
          ["Playback: start 2-1; natural lives", "ROM world="])
    check(binary, ["play", "--level", "2-1", "--inspect-check"],
          ["Playback: start 2-1; natural lives", "PASS: inspector checked 600 decisions"])
    for extra, error in [
        (["--level"], "--level needs a level"),
        (["--level", "9-1"], "spawn_levels must be all or a CSV"),
        (["--level", "4-2", "--random"], "--random cannot be combined"),
        (["4-2", "--level", "8-4"], "specify the starting level only once"),
        (["--levle", "4-2"], "unknown argument: --levle"),
    ]:
        check(binary, ["watch", checkpoint, *extra], [error], success=False)
