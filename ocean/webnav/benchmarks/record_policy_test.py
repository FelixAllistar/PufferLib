"""Exercise the existing policy encoder/decoder on native public app snapshots.

This is an interface check, not a task-success evaluation. No private row is
serialized to the policy subprocess; Python is only the qualification driver.
"""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
from public_types import U32, View, Capabilities, Action, snapshot

ROOT = Path(__file__).resolve().parents[3]


class State(C.Structure):
    _fields_ = [("version", U32), ("ref_base", U32), ("elapsed_ms", U32), ("words", U32 * 512)]


class Response(C.Structure):
    _fields_ = [("ref_base", U32), ("words", U32 * 65536)]


library = ROOT / "build/webnav/apps/record_browser/librecord_browser.so"
api = C.CDLL(str(library))
api.wa_reset.argtypes = [C.POINTER(State), U32, U32]
api.wa_step.argtypes = [C.POINTER(State), C.POINTER(Action)]
api.wa_observe.argtypes = [C.POINTER(State), C.c_char_p, C.POINTER(View), C.c_void_p, C.POINTER(Capabilities)]
for name in ("wa_reset", "wa_step", "wa_observe"):
    getattr(api, name).restype = C.c_int

response_library = ROOT / "build/webnav/primitives/response_form/libresponse_form.so"
response_api = C.CDLL(str(response_library))
response_api.wrf_reset.argtypes = [C.POINTER(Response), U32]
response_api.wrf_append.argtypes = [C.POINTER(Response), C.POINTER(View), C.POINTER(Capabilities)]
response_api.wrf_step.argtypes = [C.POINTER(Response), C.POINTER(Action)]
response_api.wrf_phase.argtypes = [C.POINTER(Response)]
response_api.wrf_expire.argtypes = [C.POINTER(Response)]
response_api.wrf_json.argtypes = [C.POINTER(Response), C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
for name in ("wrf_reset", "wrf_append", "wrf_step", "wrf_phase", "wrf_expire", "wrf_json"):
    getattr(response_api, name).restype = C.c_int


def public_snapshot(state, response=None):
    view, caps = View(), Capabilities()
    assert api.wa_observe(C.byref(state), b"Inspect records and change an amount.", C.byref(view), None, C.byref(caps)) == 0
    if response is not None:
        assert response_api.wrf_append(C.byref(response), C.byref(view), C.byref(caps)) == 0
    # The host owns the episode budget. This standalone application has no
    # task deadline or reward; setting the RPC budget does not alter its state.
    return snapshot(view, caps, 120000)


def request(process, item):
    process.stdin.write(json.dumps(item, ensure_ascii=False, separators=(",", ":")) + "\n")
    process.stdin.flush()
    line = process.stdout.readline()
    assert line, "policy RPC terminated without a response"
    result = json.loads(line)
    assert "error" not in result, result
    return result


binary = ROOT / "build/webnav/benchmarks/policy_rpc"
policies = ["random", "checkpoints/webnav_unified/1790999586398/0000000000998400.bin"]
results = []
for policy in policies:
    process = subprocess.Popen([str(binary), policy, "--sample"], cwd=ROOT,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    counts = {"policy": policy, "choices": 0, "public_actions": 0, "local_choices": 0, "worlds": 8,
              "form_actions": 0, "explicit_submissions": 0, "budget_expiries_without_response": 0}
    try:
        for trial in range(8):
            seed = (7, 29, 101, 997)[trial % 4]
            state = State()
            assert api.wa_reset(C.byref(state), seed, 4096 + seed * 65536) == 0
            response = Response() if trial >= 4 else None
            if response is not None:
                assert response_api.wrf_reset(C.byref(response), 0xFFF00000) == 0
            assert request(process, {"reset": True, "seed": seed}) == {"reset": True}
            for _ in range(64):
                observation = public_snapshot(state, response)
                command = request(process, observation)
                counts["choices"] += 1
                if command["local"]:
                    counts["local_choices"] += 1
                    continue
                payload = command["text"].encode("utf-8")
                action = Action(command["kind"], command["target"], command["arg0"], command["arg1"],
                                50, payload, len(payload))
                if response is not None and response.ref_base <= command["target"] < response.ref_base + 32:
                    assert response_api.wrf_step(C.byref(response), C.byref(action)) == 0, (command, observation)
                    counts["form_actions"] += 1
                    # The host advances application time during form interaction.
                    wait = Action(0, 0, 0, 0, 50, None, 0)
                    assert api.wa_step(C.byref(state), C.byref(wait)) == 0
                else:
                    assert api.wa_step(C.byref(state), C.byref(action)) == 0, (command, observation)
                counts["public_actions"] += 1
                if response is not None and response_api.wrf_phase(C.byref(response)) == 1:
                    size = C.c_size_t()
                    assert response_api.wrf_json(C.byref(response), None, 0, C.byref(size)) == 0
                    output = C.create_string_buffer(size.value + 1)
                    assert response_api.wrf_json(C.byref(response), output, len(output), C.byref(size)) == 0
                    submitted = json.loads(output.raw[:size.value])
                    assert set(submitted) == {"task_type", "status", "retrieved_data", "error_details"}
                    counts["explicit_submissions"] += 1
                    break
            if response is not None and response_api.wrf_phase(C.byref(response)) == 0:
                assert response_api.wrf_expire(C.byref(response)) == 0
                assert response_api.wrf_json(C.byref(response), None, 0, None) < 0
                counts["budget_expiries_without_response"] += 1
        assert counts["public_actions"] > 0
    finally:
        process.stdin.close()
        try:
            status = process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
        assert status == 0, status
    if policy != "random":
        counts["checkpoint_sha256"] = hashlib.sha256((ROOT / policy).read_bytes()).hexdigest()
    results.append(counts)

report = {"completed_at": datetime.now(timezone.utc).isoformat(),
          "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
          "response_library_sha256": hashlib.sha256(response_library.read_bytes()).hexdigest(),
          "rpc_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
          "test_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          "public_types_sha256": hashlib.sha256((Path(__file__).parent / "public_types.py").read_bytes()).hexdigest(),
          "purpose": "public action interface qualification; no rewards or success scores", "results": results}
(library.parent / "policy-validation.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"PASS: unchanged random/checkpoint RPC drives 16 application/form episodes through {sum(r['choices'] for r in results)} public policy choices")
