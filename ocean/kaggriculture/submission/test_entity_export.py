"""Public-state adapter parity, not official Kaggle or model-logit qualification."""
import ctypes as C
import json
from pathlib import Path
import subprocess

import numpy as np
import pytest

import entity_agent as agent

HERE = Path(__file__).resolve().parent


@pytest.fixture(scope="module")
def libraries(tmp_path_factory):
    build = tmp_path_factory.mktemp("entity_export")
    for name in ["entity_bridge", "entity_export_oracle"]:
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(HERE / f"{name}.c"),
            "-lm", "-o", str(build / f"{name}.so")], check=True)
    lib = C.CDLL(str(build / "entity_export_oracle.so"))
    ptr, integer = C.c_void_p, C.c_int
    for name, args, result in [
        ("oracle_create", [integer], ptr), ("oracle_snapshot", [ptr], ptr),
        ("oracle_string_free", [ptr], None), ("oracle_view", [ptr, integer, ptr], None),
        ("oracle_step", [ptr, ptr], None), ("oracle_rng", [ptr, C.c_uint32], None),
        ("export_action", [ptr, integer, ptr, integer, C.POINTER(agent.Action)], None),
        ("export_debug", [ptr, ptr, ptr], None), ("export_free", [ptr], None),
        ("export_rng", [ptr], C.c_uint32)]:
        function = getattr(lib, name)
        function.argtypes, function.restype = args, result
    return build, lib


@pytest.mark.parametrize("seed", [7, 42])
@pytest.mark.parametrize("deterministic", [False, True])
def test_public_state_controller(libraries, seed, deterministic):
    build, lib = libraries
    context = lib.oracle_create(seed)
    controllers = [agent.Controller(build / "entity_bridge.so") for _ in range(2)]
    rng = np.random.default_rng(seed)
    expected = np.empty(1424, np.float32)
    aa, ba = np.empty(47, np.float32), np.empty(47, np.float32)
    am, bm = np.empty(1978, np.uint8), np.empty(1978, np.uint8)
    try:
        for step in range(719):
            raw = lib.oracle_snapshot(context)
            state = json.loads(C.string_at(raw))
            lib.oracle_string_free(raw)
            pair = (agent.Action * 2)()
            for seat, controller in enumerate(controllers):
                public = {k: v for k, v in state.items() if k not in ("privates", "done")}
                public.update(player=seat, private=state["privates"][seat])
                actual = controller.observe(public).copy()
                lib.oracle_view(context, seat, expected.ctypes.data)
                np.testing.assert_array_equal(actual, expected,
                    err_msg=f"seed={seed} step={step} seat={seat}")
                # Re-reading a frame must not double-count observation history.
                np.testing.assert_array_equal(controller.observe(public), actual)
                logits = rng.normal(size=1979).astype(np.float32)
                lib.oracle_rng(context, controller.lib.export_rng(controller.ctx))
                action = controller.act(seat, logits, deterministic)
                lib.export_action(context, seat, logits.ctypes.data, deterministic,
                    C.byref(pair[seat]))
                assert action == agent.action_json(pair[seat])
                controller.lib.export_debug(controller.ctx, aa.ctypes.data, am.ctypes.data)
                lib.export_debug(context, ba.ctypes.data, bm.ctypes.data)
                np.testing.assert_array_equal(aa, ba)
                np.testing.assert_array_equal(am, bm)
                assert lib.export_rng(context) == controller.lib.export_rng(controller.ctx)
            lib.oracle_step(context, C.addressof(pair))
    finally:
        for controller in controllers:
            controller.close()
        lib.export_free(context)


def test_missing_history_rejected(libraries):
    build, lib = libraries
    context = lib.oracle_create(7)
    controller = agent.Controller(build / "entity_bridge.so")
    try:
        lib.oracle_step(context, C.addressof((agent.Action * 2)()))
        raw = lib.oracle_snapshot(context)
        state = json.loads(C.string_at(raw))
        lib.oracle_string_free(raw)
        public = {k: v for k, v in state.items() if k not in ("privates", "done")}
        public.update(player=0, private=state["privates"][0])
        with pytest.raises(ValueError, match="Missing episode history"):
            controller.observe(public)
    finally:
        controller.close()
        lib.export_free(context)
