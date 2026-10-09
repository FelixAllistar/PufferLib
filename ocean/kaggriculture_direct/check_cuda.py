"""Short-lived CUDA preflight; never keep a GPU context in the BC queue parent."""
import ctypes
import ctypes.util
import os
from pathlib import Path
import sys


def load_runtime():
    candidates = [ctypes.util.find_library("cudart"), "libcudart.so",
                  str(Path(os.environ.get("CUDA_HOME", "/usr/local/cuda"))/"lib64/libcudart.so")]
    for candidate in candidates:
        if candidate:
            try:
                return ctypes.CDLL(candidate)
            except OSError:
                pass
    raise RuntimeError("cannot load libcudart; check the CUDA runtime/library path")


def check_runtime(runtime):
    for name in ("cudaGetErrorName", "cudaGetErrorString"):
        function = getattr(runtime, name)
        function.argtypes = [ctypes.c_int]
        function.restype = ctypes.c_char_p
    signatures = {
        "cudaGetDeviceCount": [ctypes.POINTER(ctypes.c_int)],
        "cudaSetDevice": [ctypes.c_int],
        "cudaStreamCreate": [ctypes.POINTER(ctypes.c_void_p)],
        "cudaMemGetInfo": [ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)],
        "cudaStreamDestroy": [ctypes.c_void_p],
    }
    for name, arguments in signatures.items():
        function = getattr(runtime, name)
        function.argtypes = arguments
        function.restype = ctypes.c_int

    def check(name, *args):
        code = getattr(runtime, name)(*args)
        if code:
            label = runtime.cudaGetErrorName(code).decode()
            detail = runtime.cudaGetErrorString(code).decode()
            raise RuntimeError(f"{name}: {label} ({code}): {detail}")

    count = ctypes.c_int()
    check("cudaGetDeviceCount", ctypes.byref(count))
    if count.value == 0:
        raise RuntimeError("no visible CUDA devices")
    check("cudaSetDevice", 0)  # The standalone BC trainer uses visible device 0.
    stream = ctypes.c_void_p()
    check("cudaStreamCreate", ctypes.byref(stream))
    try:
        free, total = ctypes.c_size_t(), ctypes.c_size_t()
        check("cudaMemGetInfo", ctypes.byref(free), ctypes.byref(total))
        print(f"CUDA preflight OK: {free.value//(1024*1024)}/{total.value//(1024*1024)} MiB free",flush=True)
    finally:
        check("cudaStreamDestroy", stream)


def main():
    try:
        check_runtime(load_runtime())
    except (RuntimeError, OSError) as error:
        print(f"CUDA preflight failed: {error}",file=sys.stderr)
        if Path("/dev/nvidia-uvm").exists():
            try:
                descriptor = os.open("/dev/nvidia-uvm", os.O_RDWR)
                os.close(descriptor)
            except OSError as device_error:
                print(f"/dev/nvidia-uvm: {device_error}",file=sys.stderr)
        print("BC has not started. Resolve CUDA/device access before retrying; "
              "nvidia-smi alone does not verify CUDA works.",file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
