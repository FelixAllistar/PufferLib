import importlib.util
from pathlib import Path
from unittest.mock import Mock

import pytest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("cuda_check", ROOT/"ocean/kaggriculture_direct/check_cuda.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def runtime():
    fake = Mock()

    def count(pointer):
        pointer._obj.value = 1
        return 0

    def memory(free, total):
        free._obj.value = 12 * 1024**3
        total._obj.value = 16 * 1024**3
        return 0

    fake.cudaGetDeviceCount.side_effect = count
    fake.cudaMemGetInfo.side_effect = memory
    fake.cudaSetDevice.return_value = 0
    fake.cudaStreamCreate.return_value = 0
    fake.cudaStreamDestroy.return_value = 0
    fake.cudaGetErrorName.return_value = b"cudaErrorUnknown"
    fake.cudaGetErrorString.return_value = b"unknown error"
    return fake


def test_success_checks_context_and_releases_stream(capsys):
    fake = runtime()
    probe.check_runtime(fake)
    assert "CUDA preflight OK: 12288/16384 MiB free" in capsys.readouterr().out
    fake.cudaSetDevice.assert_called_once_with(0)
    fake.cudaStreamCreate.assert_called_once()
    fake.cudaStreamDestroy.assert_called_once()


def test_init_failure_preserves_error_name_code_and_detail():
    fake = runtime()
    fake.cudaGetDeviceCount.side_effect = None
    fake.cudaGetDeviceCount.return_value = 999
    with pytest.raises(RuntimeError, match=r"cudaGetDeviceCount: cudaErrorUnknown \(999\): unknown error"):
        probe.check_runtime(fake)
    fake.cudaSetDevice.assert_not_called()
    fake.cudaStreamCreate.assert_not_called()


def test_stream_creation_failure_is_not_hidden():
    fake = runtime()
    fake.cudaStreamCreate.return_value = 999
    with pytest.raises(RuntimeError, match="cudaStreamCreate: cudaErrorUnknown"):
        probe.check_runtime(fake)
    fake.cudaStreamDestroy.assert_not_called()


def test_memory_query_failure_still_cleans_up_stream():
    fake = runtime()
    fake.cudaMemGetInfo.side_effect = None
    fake.cudaMemGetInfo.return_value = 999
    with pytest.raises(RuntimeError, match="cudaMemGetInfo"):
        probe.check_runtime(fake)
    fake.cudaStreamDestroy.assert_called_once()


def test_no_gpu_is_a_failure():
    fake = runtime()
    fake.cudaGetDeviceCount.side_effect = None
    fake.cudaGetDeviceCount.return_value = 0
    with pytest.raises(RuntimeError, match="no visible CUDA devices"):
        probe.check_runtime(fake)
    fake.cudaStreamCreate.assert_not_called()


def test_cli_failure_is_actionable(monkeypatch, capsys):
    def failed_load():
        raise RuntimeError("test CUDA error")

    monkeypatch.setattr(probe, "load_runtime", failed_load)
    assert probe.main() == 1
    error = capsys.readouterr().err
    assert "CUDA preflight failed: test CUDA error" in error
    assert "BC has not started" in error
