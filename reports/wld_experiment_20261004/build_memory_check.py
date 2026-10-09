"""Build the allocation-only diagnostic; does not run it or start training."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = Path(__file__).with_name("check_selfplay_memory.cu")
output = source.with_suffix("")
command = ["/usr/local/cuda/bin/nvcc", "-O2", "--threads", "2", "-arch=native",
    "-std=c++17", "-I.", "-Isrc", "-Ivendor", "-Iocean/kaggriculture",
    "-Iraylib-5.5_linux_amd64/include", "-I/usr/local/cuda/include/cccl",
    "-DPUFFER_KAGGRICULTURE", "-DKAG_WITH_PAIRED_CRITIC", "-DENV_NAME=kaggriculture",
    '-DPUFFER_ENV_NAME="kaggriculture"', '-DENV_HEADER="ocean/kaggriculture/kaggriculture.h"',
    "-Xcompiler=-fopenmp", "-Xcompiler=-Wno-narrowing", "--diag-suppress=2361",
    "--diag-suppress=111", "--diag-suppress=128", str(source),
    "raylib-5.5_linux_amd64/lib/libraylib.a", "-lcudart", "-lnccl", "-lnvidia-ml",
    "-lcublas", "-lcusolver", "-lcurand", "-lm", "-lpthread", "-lomp5", "-lGL",
    "-o", str(output)]
subprocess.run(command, cwd=root, check=True)
