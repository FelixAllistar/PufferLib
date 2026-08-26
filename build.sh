#!/bin/bash
set -e

# CUDA packages commonly install under /usr/local/cuda without adding nvcc to
# non-login WSL shells. Resolve that installation explicitly instead of letting
# dirname("" ) collapse to ./bin/nvcc when `command -v nvcc` is empty.
if [ -z "${CUDA_HOME:-}" ]; then
    CUDA_HOME="${CUDA_PATH:-}"
fi
if [ -z "$CUDA_HOME" ]; then
    NVCC_PATH="$(command -v nvcc 2>/dev/null || true)"
    if [ -n "$NVCC_PATH" ]; then
        CUDA_HOME="$(dirname "$(dirname "$NVCC_PATH")")"
    elif [ -x /usr/local/cuda/bin/nvcc ]; then
        CUDA_HOME=/usr/local/cuda
    fi
fi

# Usage:
#   ./build.sh breakout              # Full native train/eval binary (CPU envs)
#   ./build.sh breakout --gpu        # GPU env path (Env* on device; requires ocean/ENV/ENV.cu)
#   ./build.sh breakout --float      # float32 precision (required for --slowly)
#   ./build.sh breakout --cpu        # Tiny standalone CPU eval executable
#   ./build.sh breakout --debug      # Debug build
#   ./build.sh breakout --local      # Standalone executable (debug, sanitizers)
#   ./build.sh breakout --fast       # Standalone executable (optimized)
#   ./build.sh breakout --tui        # Standalone + TUI capture; pairs with tui/tui_viewer
#   ./build.sh breakout --web        # Emscripten web build
#   ./build.sh breakout --profile    # Kernel profiling binary
#   ./build.sh goofspiel --exploit   # Exact small-game exploitability
#   ./build.sh goofspiel --exploit-gpu # Batched CUDA exact exploitability
#   ./build.sh goofspiel --behavior-gpu # Exact policy JSD distances
#   ./build.sh all                   # Build all envs native and native float32

if [ -z "$1" ]; then
    echo "Usage: ./build.sh ENV_NAME [--gpu] [--cards N] [--float] [--debug] [--local|--fast|--web|--profile|--cpu]"
    exit 1
fi
ENV=$1
shift

USE_GPU_ENV=0
BUILD_CARDS=0
args=("$@")
i=0
while [ $i -lt ${#args[@]} ]; do
    arg="${args[$i]}"
    case $arg in
        --gpu) USE_GPU_ENV=1 ;;
        --cards) BUILD_CARDS=1; i=$((i+1)); GS_NUM_CARDS="${args[$i]:-}" ;;
        --cards=*) BUILD_CARDS=1; GS_NUM_CARDS="${arg#--cards=}" ;;
        --float) PRECISION="-DPRECISION_FLOAT" ;;
        --debug) DEBUG=1 ;;
        --local) MODE=local ;;
        --fast)  MODE=fast ;;
        --tui)   MODE=tui ;;
        --web)   MODE=web ;;
        --profile) MODE=profile ;;
        --exploit) MODE=exploit ;;
        --exploit-gpu) MODE=exploit_gpu ;;
        --behavior-gpu) MODE=behavior_gpu ;;
        --cpu)   MODE=cpu ;;
        *) echo "Error: unknown argument '$arg'" && exit 1 ;;
    esac
    i=$((i+1))
done

if [ "$BUILD_CARDS" = "1" ] && [ -z "${GS_NUM_CARDS:-}" ]; then
    echo "Error: --cards requires a value, e.g. --cards 13 or --cards=13" >&2
    exit 1
fi
if [ -n "${GS_NUM_CARDS:-}" ]; then
    case "$GS_NUM_CARDS" in
        ''|*[!0-9]*) echo "Error: --cards must be an integer, got '$GS_NUM_CARDS'" >&2; exit 1 ;;
    esac
    if [ "$GS_NUM_CARDS" -lt 1 ] || [ "$GS_NUM_CARDS" -gt 13 ]; then
        echo "Error: --cards must be 1..13, got $GS_NUM_CARDS" >&2
        exit 1
    fi
    if [ "$ENV" != "goofspiel" ]; then
        echo "Error: --cards is only valid for goofspiel" >&2
        exit 1
    fi
fi

if [ "$ENV" = "all" ]; then
    FAILED=""
    for env_dir in ocean/*/; do
        env=$(basename "$env_dir")
        if bash "$0" "$env" && bash "$0" "$env" --float; then
            echo "OK: $env"
        else
            echo "FAIL: $env"
            FAILED="$FAILED\n  $env"
        fi
    done

    if [ -n "$FAILED" ]; then
        echo -e "\nFailed builds:$FAILED"
    fi
    exit 0
fi

# Linux/mac
PLATFORM="$(uname -s)"
if [ "$PLATFORM" = "Linux" ]; then
    RAYLIB_NAME='raylib-5.5_linux_amd64'
    OMP_LIB=-lomp5
    SANITIZE_FLAGS=(-fsanitize=address,undefined,bounds,pointer-overflow,leak -fno-omit-frame-pointer)
    STANDALONE_LDFLAGS=(-lGL)
else
    RAYLIB_NAME='raylib-5.5_macos'
    OMP_LIB=-lomp
    SANITIZE_FLAGS=()
    STANDALONE_LDFLAGS=(-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL)
fi

CLANG_WARN=(
    -Wall
    -ferror-limit=3
    -Werror=incompatible-pointer-types
    -Werror=return-type
    -Wno-error=incompatible-pointer-types-discards-qualifiers
    -Wno-incompatible-pointer-types-discards-qualifiers
    -Wno-error=array-parameter
)

download() {
    local name=$1 url=$2
    [ -d "$name" ] && return
    echo "Downloading $name..."
    case "$url" in
        *.zip) curl -sL "$url" -o "$name.zip" && unzip -q "$name.zip" && rm "$name.zip" ;;
        *)     curl -sL "$url" -o "$name.tar.gz" && tar xf "$name.tar.gz" && rm "$name.tar.gz" ;;
    esac
}

# Headless mode drops the raylib demo and OpenMP/GL dependencies; the native
# GPU train/eval binary never references them. Set HEADLESS=1 for boxes that
# only run training (e.g. rented GPU instances).
if [ "${HEADLESS:-0}" = "1" ]; then
    RAYLIB_NAME=''
    RAYLIB_A=''
    INCLUDES=(-I./src -I./vendor)
    LINK_ARCHIVES=()
    OMP_LIB=''
    STANDALONE_LDFLAGS=()
else
    RAYLIB_URL="https://github.com/raysan5/raylib/releases/download/5.5"
    if [ "$MODE" = "web" ]; then
        RAYLIB_NAME='raylib-5.5_webassembly'
        download "$RAYLIB_NAME" "$RAYLIB_URL/$RAYLIB_NAME.zip"
    else
        download "$RAYLIB_NAME" "$RAYLIB_URL/$RAYLIB_NAME.tar.gz"
    fi

    RAYLIB_A="$RAYLIB_NAME/lib/libraylib.a"
    INCLUDES=(-I./$RAYLIB_NAME/include -I./src -I./vendor)
    LINK_ARCHIVES=("$RAYLIB_A")
fi
EXTRA_SRC=""
EXTRA_LDFLAGS=()
EXTRA_CFLAGS=()
SRC_FILE=""

if [ "$ENV" = "constellation" ]; then
    SRC_DIR="src"
    OUTPUT_NAME="seethestars"
    MODE=${MODE:-fast}
    CLANG_WARN+=(-Wno-unused-function)
elif [ "$ENV" = "cache_data" ]; then
    SRC_DIR="src"
    OUTPUT_NAME="cache_data"
    SRC_FILE="src/constellation.c"
    EXTRA_CFLAGS+=(-DPUFFER_CACHE_DATA)
    MODE=${MODE:-fast}
    CLANG_WARN+=(-Wno-unused-function)
elif [ "$ENV" = "trailer" ]; then
    SRC_DIR="trailer"
    OUTPUT_NAME="trailer/trailer"
elif [ "$ENV" = "impulse_wars" ]; then
    SRC_DIR="ocean/$ENV"
    if [ "$MODE" = "web" ]; then BOX2D_NAME='box2d-web'
    elif [ "$PLATFORM" = "Linux" ]; then BOX2D_NAME='box2d-linux-amd64'
    else BOX2D_NAME='box2d-macos-arm64'
    fi
    BOX2D_URL="https://github.com/capnspacehook/box2d/releases/latest/download"
    download "$BOX2D_NAME" "$BOX2D_URL/$BOX2D_NAME.tar.gz"
    INCLUDES+=(-I./$BOX2D_NAME/include -I./$BOX2D_NAME/src)
    LINK_ARCHIVES+=("./$BOX2D_NAME/libbox2d.a")
elif [ "$ENV" = "nethack" ]; then
    SRC_DIR="ocean/$ENV"
    EXTRA_CFLAGS+=(-DPUFFER_NETHACK)
    NLE_DIR="vendor/fast-nle"
    NLE_REPO="https://github.com/FinlaySanders/fast-nle.git"
    if [ ! -d "$NLE_DIR/src" ]; then
        echo "Cloning fast-nle from $NLE_REPO ..."
        git clone --depth 1 "$NLE_REPO" "$NLE_DIR"
    fi
    NETHACK_LIB_DIR="$(pwd)/$NLE_DIR/build"
    if [ ! -f "$NETHACK_LIB_DIR/libnethack.so" ]; then
        echo "Building libnethack.so ..."
        cmake -S "$NLE_DIR" -B "$NETHACK_LIB_DIR" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$NETHACK_LIB_DIR" --target nethack -j$(nproc)
    fi
    INCLUDES+=(-I./$NLE_DIR/include
               -I./$NLE_DIR/build/_deps/deboost_context-src/include)
    EXTRA_LDFLAGS+=(-L"$NETHACK_LIB_DIR" -lnethack
                    -Xlinker -rpath -Xlinker "$NETHACK_LIB_DIR" -ldl)
elif [ "$ENV" = "shenaniguns3d" ]; then
    SRC_DIR="ocean/$ENV"
    BOX3D_DIR="${BOX3D_DIR:-../box3d}"
    if [ ! -f "$BOX3D_DIR/build/src/libbox3d.a" ]; then
        echo "Building box3d from $BOX3D_DIR ..."
        cmake -S "$BOX3D_DIR" -B "$BOX3D_DIR/build" -DCMAKE_BUILD_TYPE=Release \
            -DBOX3D_SAMPLES=OFF -DBOX3D_UNIT_TESTS=OFF -DBOX3D_BENCHMARKS=OFF > /dev/null
        cmake --build "$BOX3D_DIR/build" --target box3d -j8 > /dev/null
    fi
    INCLUDES+=(-I"$BOX3D_DIR/include" -I"../pd64")
    LINK_ARCHIVES+=("$BOX3D_DIR/build/src/libbox3d.a")
    EXTRA_CFLAGS+=(-DB3_MAX_WORLDS=8192)
    EXTRA_LDFLAGS+=("$BOX3D_DIR/build/src/libbox3d.a")
    EXTRA_SRC="../pd64/character.c"
elif [ -d "ocean/$ENV" ]; then
    SRC_DIR="ocean/$ENV"
    if [ "$ENV" = "retro" ]; then
        EXTRA_LDFLAGS+=(-ldl)
        EXTRA_SRC+=" ocean/retro/nes_emu/*.cpp"
        INCLUDES+=(-I./ocean/retro/nes_emu -I./ocean/retro)
        # fast/local standalone uses clang++ for Nes_Emu (retro.c is C++ despite .c)
        if [ "$MODE" = "fast" ] || [ "$MODE" = "local" ]; then
            CC="clang++"
            CLANG_WARN=(-Wall -ferror-limit=3 -Wno-error=return-type)
            EXTRA_CFLAGS+=(-x c++)
        fi
    fi
else
    echo "Error: environment '$ENV' not found" && exit 1
fi

OUTPUT_NAME=${OUTPUT_NAME:-$ENV}
SRC_FILE=${SRC_FILE:-$SRC_DIR/$ENV.c}

# Standalone environment build
# -mavx2 enables AVX2 intrinsics (__m256, _mm256_*) which drive.h and
# src/pufferenv.h use directly. x86_64 only — strip if porting to ARM/Apple Silicon.
SIMD_FLAGS=(-mavx2 -mfma)
if [ -n "$DEBUG" ] || [ "$MODE" = "local" ]; then
    CLANG_OPT=(-g -O0 "${CLANG_WARN[@]}" "${SANITIZE_FLAGS[@]}" "${SIMD_FLAGS[@]}")
    NVCC_OPT="-O0 -g"
    LINK_OPT="-g"
else
# No -DNDEBUG: keep assert() active (train/sweep fail-fast with messages).
    CLANG_OPT=(-O2 "${CLANG_WARN[@]}" "${SIMD_FLAGS[@]}")
    NVCC_OPT="-O2 --threads 0"
    LINK_OPT="-O2"
fi
if [ "$MODE" = "local" ] || [ "$MODE" = "fast" ]; then
    FLAGS=(
        "${INCLUDES[@]}"
        "$SRC_FILE" $EXTRA_SRC -o "$OUTPUT_NAME"
        "${LINK_ARCHIVES[@]}"
        "${EXTRA_LDFLAGS[@]}"
        "${STANDALONE_LDFLAGS[@]}"
        -lm -lpthread -fopenmp
        -DPLATFORM_DESKTOP
        "${EXTRA_CFLAGS[@]}"
    )
    echo "Compiling $ENV..."
    ${CC:-clang} "${CLANG_OPT[@]}" "${FLAGS[@]}"
    echo "Built: ./$OUTPUT_NAME"
    exit 0
elif [ "$MODE" = "tui" ]; then
    # Terminal-over-SSH eval: same standalone binary, but c_render/puf_render
    # also streams framed RGBA (see tui/puffer_tui.h). Pair it with the viewer:
    #   xvfb-run ./$OUTPUT_NAME | ./tui_viewer --sink=ansi
    EXTRA_CFLAGS+=(-DPUFFER_TUI_CAPTURE -I./tui)
    FLAGS=(
        "${INCLUDES[@]}"
        "$SRC_FILE" $EXTRA_SRC -o "$OUTPUT_NAME"
        "${LINK_ARCHIVES[@]}"
        "${EXTRA_LDFLAGS[@]}"
        "${STANDALONE_LDFLAGS[@]}"
        -lm -lpthread -fopenmp
        -DPLATFORM_DESKTOP
        "${EXTRA_CFLAGS[@]}"
    )
    echo "Compiling $ENV (tui capture)..."
    ${CC:-clang} "${CLANG_OPT[@]}" "${FLAGS[@]}" || exit 1

    echo "Compiling tui_viewer..."
    ${CC:-clang} -O2 "${CLANG_WARN[@]}" tui/tui_viewer.c -o tui_viewer -ldl || exit 1
    echo "Built: ./$OUTPUT_NAME + ./tui_viewer"
    echo "Run:   xvfb-run -a ./$OUTPUT_NAME | ./tui_viewer --sink=ansi"
    exit 0
elif [ "$MODE" = "exploit" ]; then
    if [ "$ENV" != "goofspiel" ]; then
        echo "Error: --exploit is only available for goofspiel"
        exit 1
    fi
    ${CC:-clang} -O3 "${CLANG_WARN[@]}" "${SIMD_FLAGS[@]}" \
        -I. -Isrc -I$SRC_DIR -Ivendor \
        "$SRC_DIR/goofspiel_exploit.c" -lm -o goofspiel_exploit
    echo "Built: ./goofspiel_exploit"
    exit 0
elif [ "$MODE" = "exploit_gpu" ]; then
    if [ "$ENV" != "goofspiel" ]; then
        echo "Error: --exploit-gpu is only available for goofspiel"
        exit 1
    fi
    CUDA_HOME=${CUDA_HOME:-${CUDA_PATH:-$(dirname "$(dirname "$(which nvcc)")")}}
    ${CUDA_HOME}/bin/nvcc -O3 --threads 0 -arch=${NVCC_ARCH:-native} -std=c++17 \
        -I. -Isrc -I$SRC_DIR -Ivendor \
        "$SRC_DIR/goofspiel_exploit.cu" -L$CUDA_HOME/lib64 -lcublas -lm \
        -o goofspiel_exploit_gpu
    echo "Built: ./goofspiel_exploit_gpu"
    exit 0
elif [ "$MODE" = "behavior_gpu" ]; then
    if [ "$ENV" != "goofspiel" ]; then
        echo "Error: --behavior-gpu is only available for goofspiel"
        exit 1
    fi
    CUDA_HOME=${CUDA_HOME:-${CUDA_PATH:-$(dirname "$(dirname "$(which nvcc)")")}}
    ${CUDA_HOME}/bin/nvcc -O3 --threads 0 -arch=${NVCC_ARCH:-native} -std=c++17 \
        -I. -Isrc -I$SRC_DIR -Ivendor \
        "$SRC_DIR/goofspiel_behavior.cu" -L$CUDA_HOME/lib64 -lcublas -lm \
        -o goofspiel_behavior_gpu
    echo "Built: ./goofspiel_behavior_gpu"
    exit 0
elif [ "$MODE" = "web" ]; then
    mkdir -p "build/web/$ENV"
    echo "Compiling $ENV for web..."
    emcc \
        -o "build/web/$ENV/game.html" \
        "$SRC_FILE" $EXTRA_SRC \
        -O3 -Wall \
        "${LINK_ARCHIVES[@]}" \
        "${INCLUDES[@]}" \
        -L. -L./$RAYLIB_NAME/lib \
        -sASSERTIONS=2 -gsource-map \
        -sUSE_GLFW=3 -sUSE_WEBGL2=1 -sASYNCIFY -sFILESYSTEM -sFORCE_FILESYSTEM=1 \
        --shell-file vendor/minshell.html \
        -sINITIAL_MEMORY=512MB -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=512KB \
        -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES3 \
        --preload-file resources/$ENV@resources/$ENV \
        --preload-file resources/shared@resources/shared \
        "${EXTRA_CFLAGS[@]}"
    echo "Built: build/web/$ENV/game.html"
    exit 0
elif [ "$MODE" = "cpu" ]; then
    ENV_HEADER="$SRC_DIR/$ENV.h"
    if ! grep -q 'typedef[[:space:]].*obs_t' "$ENV_HEADER" 2>/dev/null; then
        echo "Error: $ENV_HEADER must typedef obs_t for standalone eval"
        exit 1
    fi

    echo "Compiling standalone CPU eval for $ENV..."
    ${CC:-clang} "${CLANG_OPT[@]}" \
        -I. -Isrc -I$SRC_DIR -Ivendor "${INCLUDES[@]}" \
        -DPLATFORM_DESKTOP \
        -DPUFFERCPU_EVAL_MAIN \
        -DENV_HEADER=\"$ENV_HEADER\" \
        -x c src/puffercpu.h -x none $EXTRA_SRC \
        "${LINK_ARCHIVES[@]}" \
        "${EXTRA_LDFLAGS[@]}" \
        "${STANDALONE_LDFLAGS[@]}" \
        -lm -lpthread -fopenmp \
        -o build_cpu
    echo "Built: ./build_cpu"
    exit 0
fi

CUDA_HOME=${CUDA_HOME:-${CUDA_PATH:-$(dirname "$(dirname "$(which nvcc)")")}}
# NCCL include/lib fallback.
# Needed when NCCL is provided by the nvidia-nccl-cu12 wheel in the active venv.
NCCL_IFLAG=""
NCCL_LFLAG=""
for dir in /usr/include /usr/local/cuda/include; do
    if [ -f "$dir/nccl.h" ]; then NCCL_IFLAG="-I$dir"; break; fi
done
for dir in /usr/lib/x86_64-linux-gnu /usr/local/cuda/lib64; do
    if [ -f "$dir/libnccl.so" ] || [ -f "$dir/libnccl.so.2" ]; then NCCL_LFLAG="-L$dir"; break; fi
done
if [ -z "$NCCL_IFLAG" ]; then
    NCCL_IFLAG=$(python -c "import nvidia.nccl, os; print('-I' + os.path.join(nvidia.nccl.__path__[0], 'include'))" 2>/dev/null || echo "")
fi
if [ -z "$NCCL_LFLAG" ]; then
    NCCL_LFLAG=$(python -c "import nvidia.nccl, os; print('-L' + os.path.join(nvidia.nccl.__path__[0], 'lib'))" 2>/dev/null || echo "")
fi

export CCACHE_DIR="${CCACHE_DIR:-$HOME/.ccache}"
export CCACHE_BASEDIR="$(pwd)"
export CCACHE_COMPILERCHECK=content
NVCC="ccache $CUDA_HOME/bin/nvcc"
CC="${CC:-$(command -v ccache >/dev/null && echo 'ccache clang' || echo 'clang')}"
ARCH=${NVCC_ARCH:-native}

ENV_HEADER="$SRC_DIR/$ENV.h"
mkdir -p build
if ! grep -q 'typedef[[:space:]].*obs_t' "$ENV_HEADER" 2>/dev/null; then
    echo "Error: $ENV_HEADER must typedef obs_t"
    exit 1
fi

ENV_COMPILE_FLAGS=(-DENV_HEADER=\"$ENV_HEADER\")
if [ "${KAG_DUMP_ROOT_OBS:-0}" = "1" ]; then
    ENV_COMPILE_FLAGS+=(-DKAG_DUMP_ROOT_OBS)
fi
# Goofspiel compile-time ABI: 4-card (default) or 13-card training layout.
if [ "$ENV" = "goofspiel" ]; then
    ENV_COMPILE_FLAGS+=(-DGS_NUM_CARDS=${GS_NUM_CARDS:-4})
fi
# GPU env is compile-time exclusive (not a runtime dual path with CPU workers).
if [ "$USE_GPU_ENV" = "1" ]; then
    GPU_ENV_HEADER="$SRC_DIR/$ENV.cu"
    if [ ! -f "$GPU_ENV_HEADER" ]; then
        echo "Error: --gpu requires $GPU_ENV_HEADER"
        exit 1
    fi
    ENV_COMPILE_FLAGS+=(-DPUFFER_GPU_ENV -DGPU_ENV_HEADER=\"$GPU_ENV_HEADER\")
fi

MODE=${MODE:-native}

if [ "$MODE" = "native" ]; then
    echo "Compiling native train/eval binary ($ARCH)..."
    OMP_FLAG=()
    if [ "${HEADLESS:-0}" != "1" ]; then
        OMP_FLAG=(-Xcompiler=-fopenmp)
    fi
    $NVCC $NVCC_OPT -arch=$ARCH -std=c++17 \
        -I. -Isrc -I$SRC_DIR -Ivendor \
        "${INCLUDES[@]}" \
        -I$CUDA_HOME/include -I$CUDA_HOME/include/cccl $NCCL_IFLAG \
        ${RAYLIB_NAME:+-I./$RAYLIB_NAME/include} \
	    "${ENV_COMPILE_FLAGS[@]}" \
	    -DENV_NAME=$ENV \
	    -DPUFFER_ENV_NAME=\"$ENV\" \
	    -DPUFFERLIB_BUILD_MAIN \
	    -Xcompiler=-DPLATFORM_DESKTOP \
	    "${OMP_FLAG[@]}" \
	    "${EXTRA_CFLAGS[@]}" \
	    $PRECISION \
	    src/pufferl.cu \
        $EXTRA_SRC \
        ${RAYLIB_A:+"$RAYLIB_A"} \
        -L$CUDA_HOME/lib64 $NCCL_LFLAG \
        "${EXTRA_LDFLAGS[@]}" \
        -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand \
        -lm -lpthread $OMP_LIB "${STANDALONE_LDFLAGS[@]}" \
        -o puffer
    echo "Built: ./puffer"
    if [ "$ENV" = "kaggriculture" ] && [ "${HEADLESS:-0}" != "1" ]; then
        bash "$0" "$ENV" --fast
    fi

elif [ "$MODE" = "profile" ]; then
    echo "Compiling profile binary ($ARCH)..."
    $NVCC $NVCC_OPT -arch=$ARCH -std=c++17 \
        -I. -Isrc -I$SRC_DIR -Ivendor \
        "${INCLUDES[@]}" \
        -I$CUDA_HOME/include -I$CUDA_HOME/include/cccl $NCCL_IFLAG -I$RAYLIB_NAME/include \
        "${ENV_COMPILE_FLAGS[@]}" \
        -DENV_NAME=$ENV \
	    -DPUFFER_ENV_NAME=\"$ENV\" \
        -Xcompiler=-DPLATFORM_DESKTOP \
	    "${EXTRA_CFLAGS[@]}" \
        $PRECISION \
        -Xcompiler=-fopenmp \
        tests/profile_kernels.cu \
        "$RAYLIB_A" \
        -L$CUDA_HOME/lib64 \
        -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand \
        -lGL -lm -lpthread $OMP_LIB \
        -o profile
    echo "Built: ./profile"
fi
