#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."
bench_env="${PK_BENCHMARK_ENV:-build/pokemon/oak-benchmark-env}"
if [[ ! -x "$bench_env/bin/python" ]]; then uv venv "$bench_env"; fi
# The search-only benchmark needs the native wheel and numpy, not torch/CUDA.
uv pip install --python "$bench_env/bin/python" --no-deps 'oak-lab==1.1.4' 'numpy==2.5.4'
bash ocean/pokemon/build_engine.sh
bench_blas=$("$bench_env/bin/python" -c 'import pathlib,numpy; print(next((pathlib.Path(numpy.__file__).parent.parent/"numpy.libs").glob("libscipy_openblas64_*.so")))')
bench_libdir=$(dirname "$bench_blas")
bench_common=(-O3 -mavx2 -mfma -Isrc -Iraylib-5.5_linux_amd64/include)
bench_links=(build/pokemon/libpokemon.a raylib-5.5_linux_amd64/lib/libraylib.a -lm -lGL -lpthread -ldl -lrt)
clang "${bench_common[@]}" ocean/pokemon/pokemon.c "${bench_links[@]}" -o build/pokemon/benchmark_viewer
clang "${bench_common[@]}" -shared -fPIC ocean/pokemon/benchmarks/adapter.c "${bench_links[@]}" -o build/pokemon/benchmark_adapter.so
clang "${bench_common[@]}" -DPK_CPU_BENCHMARK_GEMM ocean/pokemon/pokemon.c ocean/pokemon/benchmarks/blas.c \
    "${bench_links[@]}" "$bench_blas" -Wl,-rpath,"$bench_libdir" -o build/pokemon/benchmark_viewer_blas
clang "${bench_common[@]}" -DPK_CPU_BENCHMARK_GEMM -shared -fPIC ocean/pokemon/benchmarks/adapter.c ocean/pokemon/benchmarks/blas.c \
    "${bench_links[@]}" "$bench_blas" -Wl,-rpath,"$bench_libdir" -o build/pokemon/benchmark_adapter_blas.so
printf 'Benchmark tools built. Python: %s/bin/python\n' "$bench_env"
