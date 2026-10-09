#!/usr/bin/env bash
# Dedicated benchmark Docker daemon. Its ext4 backing file lives on Windows D:.
set -euo pipefail
bench_root=/mnt/d/puffertank/webnav-bench
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
bench_bin="$repo_dir/build/webnav/benchmarks/docker"
bench_mount=/mnt/webnav-bench-docker
bench_socket=unix:///run/webnav-bench/docker.sock
bench_docker="$bench_bin/docker"
IFS= read -r bench_image < "$repo_dir/ocean/webnav/benchmarks/image.txt"
case "${1:-status}" in
  install)
    [ "$(id -u)" = 0 ] || { echo 'install requires root in WSL' >&2; exit 2; }
    mkdir -p "$bench_root" "$bench_mount" /run/webnav-bench
    # Never format an existing file or device.
    if [ ! -e "$bench_root/docker-data.ext4" ]; then
      truncate -s 20G "$bench_root/docker-data.ext4"
      mkfs.ext4 -q -F -m 0 -E nodiscard "$bench_root/docker-data.ext4"
    fi
    if ! mountpoint -q "$bench_mount"; then
      mount -o loop "$bench_root/docker-data.ext4" "$bench_mount"
    fi
    if [ ! -x "$bench_docker" ]; then
      mkdir -p "$repo_dir/build/webnav/benchmarks"
      tar -xzf "$bench_root/docker-29.8.2.tgz" -C "$repo_dir/build/webnav/benchmarks"
    fi
    # Separate socket, PID, state and storage. No system Docker configuration edits.
    if ! systemctl is-active --quiet webnav-bench-docker.service; then
      systemd-run --unit=webnav-bench-docker --slice=webnav-benchmark.slice \
        --property=MemoryMax=1536M --property=MemorySwapMax=0 --property=TasksMax=128 \
        --property=CPUQuota=100% --collect \
        --setenv="PATH=$bench_bin:/usr/sbin:/usr/bin:/sbin:/bin" \
        "$bench_bin/dockerd" --host "$bench_socket" --group felix \
        --data-root "$bench_mount/data" --exec-root /run/webnav-bench/exec \
        --pidfile /run/webnav-bench/dockerd.pid --storage-driver overlay2 \
        --exec-opt native.cgroupdriver=systemd --cgroup-parent=webnav-benchmark.slice \
        --default-ulimit core=0:0
      systemctl set-property --runtime webnav-benchmark.slice \
        MemoryMax=3G MemorySwapMax=0 TasksMax=512 CPUQuota=200%
    fi
    ;;
  pull)
    exec "$bench_docker" --host "$bench_socket" pull "$bench_image"
    ;;
  start)
    if "$bench_docker" --host "$bench_socket" container inspect webnav-bench-admin >/dev/null 2>&1; then
      exec "$bench_docker" --host "$bench_socket" start webnav-bench-admin
    fi
    exec "$bench_docker" --host "$bench_socket" run -d --name webnav-bench-admin \
      --memory=2304m --memory-swap=2304m --cpus=2 --pids-limit=256 \
      --cgroup-parent=webnav-benchmark.slice \
      -e WA_ENV_CTRL_EXTERNAL_SITE_URL=http://127.0.0.1:7780/ \
      -p 127.0.0.1:7780:80 -p 127.0.0.1:7781:8877 \
      "$bench_image"
    ;;
  stop) exec "$bench_docker" --host "$bench_socket" stop webnav-bench-admin ;;
  reset)
    if "$bench_docker" --host "$bench_socket" container inspect webnav-bench-admin >/dev/null 2>&1; then
      "$bench_docker" --host "$bench_socket" rm -f webnav-bench-admin
    fi
    exec bash "$0" start
    ;;
  status) exec "$bench_docker" --host "$bench_socket" ps -a ;;
  docker) shift; exec "$bench_docker" --host "$bench_socket" "$@" ;;
  *) echo 'Use install, pull, start, stop, reset, status or docker ARGS' >&2; exit 2 ;;
esac
