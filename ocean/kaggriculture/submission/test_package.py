import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile

import numpy as np
import pytest

from test_entity_export import libraries

HERE = Path(__file__).resolve().parent


@pytest.fixture
def inputs(tmp_path):
    checkpoint = tmp_path / "model.bin"
    # H32/L1 canonical layout, including padding and recurrent weights.
    np.zeros(105736, dtype="<f4").tofile(checkpoint)
    config = tmp_path / "config.ini"
    config.write_text("[base]\nenv_name=kaggriculture\n[policy]\nhidden_size=32\n"
        "num_layers=1\n[env]\nmarket_slots=10\nmax_hands=16\nland_buy_min_days=0\n")
    return checkpoint, config


@pytest.mark.parametrize("sampling", ["deterministic", "stochastic"])
def test_archive_loads_without_file_global(tmp_path, inputs, libraries, sampling):
    import ctypes as C

    checkpoint, config = inputs
    archive = tmp_path / "submission.tar.gz"
    command = [sys.executable, str(HERE / "package.py"), "--checkpoint", str(checkpoint),
        "--config", str(config), "--output", str(archive), "--sampling", sampling]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    stage = tmp_path / "unpacked"
    with tarfile.open(archive) as bundle:
        assert set(bundle.getnames()) == {"main.py", "model.bin", "entity_bridge.so",
            "policy_metadata.json"}
        bundle.extractall(stage, filter="data")
    metadata = json.loads((stage / "policy_metadata.json").read_text())
    assert metadata["deterministic"] == (sampling == "deterministic")
    assert metadata["official_runtime_verified"] is False
    assert (stage / "model.bin").read_bytes() == checkpoint.read_bytes()
    for name, digest in metadata["files_sha256"].items():
        assert hashlib.sha256((stage / name).read_bytes()).hexdigest() == digest
    namespace = {"__name__": "test_packaged_agent"}
    source = stage / "main.py"
    exec(compile(source.read_text(), str(source), "exec"), namespace)
    _, lib = libraries
    context = lib.oracle_create(7)
    try:
        raw = lib.oracle_snapshot(context)
        state = json.loads(C.string_at(raw))
        lib.oracle_string_free(raw)
        observation = {k: v for k, v in state.items() if k not in ("privates", "done")}
        observation.update(player=0, private=state["privates"][0])
        action = namespace["agent"](observation)
        assert set(action) == {"farmer", "hands", "market"}
        before = namespace["_MODEL"].state.copy()
        assert namespace["agent"](observation) == action
        np.testing.assert_array_equal(namespace["_MODEL"].state, before)
    finally:
        if namespace["_CONTROLLER"]:
            namespace["_CONTROLLER"].close()
        lib.export_free(context)
    before = archive.read_bytes()
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert archive.read_bytes() == before


def test_controller_mismatch_rejected(tmp_path, inputs):
    checkpoint, config = inputs
    config.write_text(config.read_text().replace("max_hands=16", "max_hands=8"))
    output = tmp_path / "invalid.tar.gz"
    result = subprocess.run([sys.executable, str(HERE / "package.py"),
        "--checkpoint", str(checkpoint), "--config", str(config), "--output", str(output),
        "--sampling", "deterministic"], capture_output=True, text=True)
    assert result.returncode != 0 and "unsupported controller setting" in result.stderr
    assert not output.exists()


@pytest.mark.parametrize("invalid", ["truncated", "nonfinite"])
def test_bad_checkpoint_is_not_published(tmp_path, inputs, invalid):
    checkpoint, config = inputs
    weights = np.fromfile(checkpoint, "<f4")
    if invalid == "truncated":
        weights = weights[:-1]
    else:
        weights[0] = np.nan
    weights.tofile(checkpoint)
    output = tmp_path / "invalid.tar.gz"
    result = subprocess.run([sys.executable, str(HERE / "package.py"),
        "--checkpoint", str(checkpoint), "--config", str(config), "--output", str(output),
        "--sampling", "deterministic"], capture_output=True, text=True)
    assert result.returncode != 0
    assert not output.exists()
    assert not list(tmp_path.glob("kag-package-*"))
