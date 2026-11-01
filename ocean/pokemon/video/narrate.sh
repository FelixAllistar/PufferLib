#!/usr/bin/env bash
# Voice the build's pending narration lines (narration/todo.json) with Qwen3-TTS on the poker box (2026-11-01).
# Usage: narrate.sh MEDIA_DIR   (then run build.py again)
# The box comes from TTS_HOST (user@host), TTS_PORT and TTS_KEY, e.g. set in the gitignored .env.
set -e
[ -f "$(git rev-parse --show-toplevel)/.env" ] && . "$(git rev-parse --show-toplevel)/.env"
: "${TTS_HOST:?set TTS_HOST (user@host of the GPU box)}"
M=${1:-/mnt/d/puffer_media/pokemon_gen1}
N=$M/video/narration
SSH="ssh -i ${TTS_KEY:-$HOME/.ssh/vast_ed25519} -p ${TTS_PORT:-22} $TTS_HOST"
SCP="scp -q -i ${TTS_KEY:-$HOME/.ssh/vast_ed25519} -P ${TTS_PORT:-22}"
$SCP "$(dirname "$0")/tts_qwen.py" $N/todo.json "$TTS_HOST:/workspace/tts/"
$SSH 'cd /workspace/tts && rm -rf out_todo && HF_HOME=/dev/shm/hf OMP_NUM_THREADS=4 .venv/bin/python tts_qwen.py todo.json out_todo 2>&1 | grep -v -i -E "warn|pad_token"' 2>&1 | grep -v -i -E "vast.ai|have fun|AGENTS"
$SCP "$TTS_HOST:/workspace/tts/out_todo/*.wav" $N/
rm $N/todo.json
