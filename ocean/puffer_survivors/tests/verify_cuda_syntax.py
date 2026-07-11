#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sources = [
    root / "cuda/ps_cuda_sim.cu",
    root / "cuda/ps_cuda_vec.cu",
    root / "tests/bench_cuda.cu",
]

with tempfile.TemporaryDirectory() as td:
    out_files = []
    for src in sources:
        text = src.read_text()
        # Host-C++ syntax pass: preserve kernel bodies but replace launch syntax.
        text = re.sub(
            r"([A-Za-z_][A-Za-z0-9_]*)\s*<<<.*?>>>\s*\((.*?)\);",
            lambda m: f"{m.group(1)}({m.group(2)});",
            text,
            flags=re.S,
        )
        out = Path(td) / (src.stem + ".cpp")
        out.write_text(text)
        out_files.append(str(out))

    cmd = [
        "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
        f"-I{root / 'tests/cuda_stub'}", f"-I{root / 'cuda'}", f"-I{root}",
        "-fsyntax-only", *out_files,
    ]
    proc = subprocess.run(cmd, text=True)
    sys.exit(proc.returncode)
