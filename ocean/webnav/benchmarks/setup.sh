#!/usr/bin/env bash
# Reference/scorer/browser-client assets only. Website lifecycle is runtime.sh.
set -euo pipefail
cd "$(dirname "$0")/../../.."
bench_root=/mnt/d/puffertank/webnav-bench
revision=6473f72db5dcefc97b5725b59e734504edc28a21
mkdir -p "$bench_root/node"
if [ ! -d "$bench_root/webarena-verified/.git" ]; then
  git clone --filter=blob:none --no-checkout https://github.com/ServiceNow/webarena-verified.git "$bench_root/webarena-verified"
  git -C "$bench_root/webarena-verified" fetch --depth 1 origin "$revision"
  git -C "$bench_root/webarena-verified" checkout --detach "$revision"
fi
test "$(git -C "$bench_root/webarena-verified" rev-parse HEAD)" = "$revision"
git -C "$bench_root/webarena-verified" diff --exit-code
if [ ! -x "$bench_root/scorer-venv/bin/python" ]; then
  UV_CACHE_DIR="$bench_root/uv-cache" uv venv "$bench_root/scorer-venv"
fi
UV_CACHE_DIR="$bench_root/uv-cache" uv pip install \
  --python "$bench_root/scorer-venv/bin/python" \
  --constraint ocean/webnav/benchmarks/scorer-requirements.txt "$bench_root/webarena-verified"
npm install --prefix "$bench_root/node" --ignore-scripts --no-audit --no-fund --save-exact playwright-core@1.63.0
docker_sum=995d1ef289677f74fd58d8d2c35727b6a4ee389c69db8638a3e42d0487aa5b0f
archive="$bench_root/docker-29.8.2.tgz"
if ! printf '%s  %s\n' "$docker_sum" "$archive" | sha256sum --check --status; then
  curl -fL --retry 2 https://download.docker.com/linux/static/stable/x86_64/docker-29.8.2.tgz -o "$archive.part"
  printf '%s  %s\n' "$docker_sum" "$archive.part" | sha256sum --check
  mv "$archive.part" "$archive"
fi
