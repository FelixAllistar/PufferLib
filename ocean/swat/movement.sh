#!/usr/bin/env bash
set -euo pipefail
SWAT_MOVEMENT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$SWAT_MOVEMENT_ROOT"

# CUDA policy training stays native on Linux/WSL. No Python interpreter.
export CUDA_HOME="${CUDA_HOME:-${CUDA_PATH:-/usr/local/cuda}}"
export PATH="$CUDA_HOME/bin:$PATH"
export LD_LIBRARY_PATH="$CUDA_HOME/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -d /usr/lib/wsl/lib ]; then
    export LD_LIBRARY_PATH="/usr/lib/wsl/lib:$LD_LIBRARY_PATH"
    mkdir -p build/swat/native-libs
    if [ -f /usr/lib/wsl/lib/libnvidia-ml.so.1 ]; then
        ln -sfn /usr/lib/wsl/lib/libnvidia-ml.so.1 build/swat/native-libs/libnvidia-ml.so
    fi
    export LIBRARY_PATH="$SWAT_MOVEMENT_ROOT/build/swat/native-libs${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi
SWAT_MOVEMENT_COMMAND=${1:-help}
if [ $# -gt 0 ]; then shift; fi
case "$SWAT_MOVEMENT_COMMAND" in
    build|build-trainer|build-evaluator)
        mkdir -p build/swat
        if [ "$SWAT_MOVEMENT_COMMAND" != build-evaluator ]; then
            bash build.sh swat_movement build/swat/movement-puffer --float
        fi
        if [ "$SWAT_MOVEMENT_COMMAND" != build-trainer ]; then
            bash build.sh swat_movement build/swat/movement-eval --cpu
        fi
        ;;
    train)
        exec ./build/swat/movement-puffer train "$@"
        ;;
    eval|watch|play)
        if [ $# -lt 1 ]; then echo "Usage: $0 $SWAT_MOVEMENT_COMMAND CHECKPOINT.bin [--section.key=value ...]" >&2; exit 1; fi
        SWAT_MOVEMENT_CHECKPOINT=$1; shift
        case "$SWAT_MOVEMENT_COMMAND" in
            eval) exec ./build/swat/movement-puffer eval --headless "--base.load_model_path=$SWAT_MOVEMENT_CHECKPOINT" "$@" ;;
            watch) exec ./build/swat/movement-eval "$SWAT_MOVEMENT_CHECKPOINT" "$@" ;;
            play) exec bash ocean/swat/play.sh play --locomotion-policy "$SWAT_MOVEMENT_CHECKPOINT" "$@" ;;
        esac
        ;;
    *)
        echo "Edit config/swat_movement.ini. Commands:"
        echo "  bash $0 build"
        echo "  bash $0 train [--section.key=value ...]"
        echo "  bash $0 eval CHECKPOINT.bin [--section.key=value ...]"
        echo "  bash $0 watch CHECKPOINT.bin [--section.key=value ...]"
        echo "  bash $0 play CHECKPOINT.bin [--mission building ...]"
        [ "$SWAT_MOVEMENT_COMMAND" = help ]
        ;;
esac
