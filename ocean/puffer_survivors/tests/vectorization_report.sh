#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cc="${CC:-gcc}"
out="${1:-$root/tests/vectorization_report.txt}"
missed="${out%.txt}_missed.txt"

"$cc" -O3 -march=native -std=c17 -DNDEBUG -I"$root" \
  "$root/tests/bench_cpu.c" -lm -o /tmp/ps_bench_vectorization \
  -fopt-info-vec-optimized="$out"

"$cc" -O3 -march=native -std=c17 -DNDEBUG -I"$root" \
  "$root/tests/bench_cpu.c" -lm -o /tmp/ps_bench_vectorization \
  -fopt-info-vec-missed="$missed" >/dev/null 2>&1 || true

printf 'optimized report: %s\nmissed report:    %s\n' "$out" "$missed"
