"""Check native submissions against the pinned public response model only.

This qualification test does not load task instances or evaluator answers.
Python stays outside the simulator, policy and learner.
"""
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
from datetime import datetime, timezone

import pydantic
from public_types import U32, View, Capabilities, Action, text


ROOT = Path(__file__).resolve().parents[3]
REFERENCE = Path(os.environ.get("WEBNAV_REFERENCE_ROOT", "/mnt/d/puffertank/webnav-bench/webarena-verified"))
SOURCE_PATH = "src/webarena_verified/types/agent_response.py"
manifest = json.loads((ROOT / "ocean/webnav/benchmarks/manifest.json").read_text())
source = REFERENCE / SOURCE_PATH
expected_source = subprocess.check_output(
    ["git", "-C", str(REFERENCE), "show", f"{manifest['revision']}:{SOURCE_PATH}"]
)
assert source.read_bytes() == expected_source, "public schema differs from the pinned revision"
spec = importlib.util.spec_from_file_location("pinned_public_response", source)
public = importlib.util.module_from_spec(spec)
spec.loader.exec_module(public)
library = ROOT / "build/webnav/primitives/finish/libfinish.so"
api = C.CDLL(str(library))


class State(C.Structure):
    _fields_ = [("words", C.c_uint32 * 65536)]


class Reply(C.Structure):
    _fields_ = [
        ("kind", C.c_uint32), ("status", C.c_uint32),
        ("data_json", C.c_char_p), ("data_bytes", C.c_size_t),
        ("detail_json", C.c_char_p), ("detail_bytes", C.c_size_t),
    ]


class Form(C.Structure):
    _fields_ = [("ref_base", U32), ("words", U32 * 65536)]


api.wfn_reset.argtypes = [C.POINTER(State)]
api.wfn_submit.argtypes = [C.POINTER(State), C.POINTER(Reply)]
api.wfn_expire.argtypes = [C.POINTER(State)]
api.wfn_json.argtypes = [C.POINTER(State), C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
for name in ("wfn_reset", "wfn_submit", "wfn_expire", "wfn_json"):
    getattr(api, name).restype = C.c_int

form_library = ROOT / "build/webnav/primitives/response_form/libresponse_form.so"
form_api = C.CDLL(str(form_library))
form_api.wrf_reset.argtypes = [C.POINTER(Form), U32]
form_api.wrf_observe.argtypes = [C.POINTER(Form), C.POINTER(View), C.POINTER(Capabilities)]
form_api.wrf_step.argtypes = [C.POINTER(Form), C.POINTER(Action)]
form_api.wrf_json.argtypes = [C.POINTER(Form), C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
for name in ("wrf_reset", "wrf_observe", "wrf_step", "wrf_json"):
    getattr(form_api, name).restype = C.c_int


def form_emit(kind, status, data, detail):
    form = Form()
    assert form_api.wrf_reset(C.byref(form), 0xFFF00000) == 0

    def act(kind, role, label, argument=0, payload=b""):
        view, caps = View(), Capabilities()
        assert form_api.wrf_observe(C.byref(form), C.byref(view), C.byref(caps)) == 0
        node = next(n for n in view.nodes[:view.count] if n.role == role and text(view, n.name) == label)
        assert any(c.kind == kind and c.ref == node.ref for c in caps.items[:caps.count])
        action = Action(kind, node.ref, argument, 0, 0, payload, len(payload))
        assert form_api.wrf_step(C.byref(form), C.byref(action)) == 0

    act(21, 6, "Task type", kind)
    act(21, 6, "Outcome status", status)
    for label, value in (("Retrieved data (JSON)", data), ("Error details (JSON)", detail)):
        act(1, 16, label)
        act(9, 16, label)
        chunk = b""
        for cp in value.decode("utf-8"):
            encoded = cp.encode("utf-8")
            if len(chunk) + len(encoded) > 255:
                act(2, 16, label, payload=chunk)
                chunk = b""
            chunk += encoded
        if chunk:
            act(2, 16, label, payload=chunk)
    act(1, 1, "Finish")
    size = C.c_size_t()
    assert form_api.wrf_json(C.byref(form), None, 0, C.byref(size)) == 0
    output = C.create_string_buffer(size.value + 1)
    assert form_api.wrf_json(C.byref(form), output, len(output), C.byref(size)) == 0
    return output.raw[:size.value]

KINDS = ["NAVIGATE", "MUTATE", "RETRIEVE"]
STATUSES = ["SUCCESS", "ACTION_NOT_ALLOWED_ERROR", "PERMISSION_DENIED_ERROR",
            "NOT_FOUND_ERROR", "DATA_VALIDATION_ERROR", "UNKNOWN_ERROR"]
state = State()
checked = 0


def check(kind, status, data, detail):
    global checked
    data, detail = data.encode("utf-8"), detail.encode("utf-8")
    assert api.wfn_reset(C.byref(state)) == 0
    reply = Reply(kind, status, data, len(data), detail, len(detail))
    assert api.wfn_submit(C.byref(state), C.byref(reply)) == 0
    size = C.c_size_t()
    assert api.wfn_json(C.byref(state), None, 0, C.byref(size)) == 0
    output = C.create_string_buffer(size.value + 1)
    assert api.wfn_json(C.byref(state), output, len(output), C.byref(size)) == 0
    raw = output.raw[:size.value]
    expected = {"task_type": KINDS[kind], "status": STATUSES[status],
                "retrieved_data": json.loads(data), "error_details": json.loads(detail)}
    parsed = public.FinalAgentResponse.model_validate_json(raw).model_dump()
    assert parsed == expected, (parsed, expected)
    # Preserve the submitted JSON text, including precision and Unicode escapes.
    assert b'"retrieved_data":' + data + b',"error_details":' + detail + b'}' in raw
    form_raw = form_emit(kind, status, data, detail)
    assert form_raw == raw, "public form actions changed the submitted payload"
    assert public.FinalAgentResponse.model_validate_json(form_raw).model_dump() == expected
    checked += 1


for kind in range(3):
    for status in range(6):
        check(kind, status, "[]" if kind == 2 else "null",
              '"The requested record was unavailable."' if status else "null")

for data in ["null", "[]", "[true,false]", "[9007199254740993,-9223372036854775809]",
             "[12.50,1e-3]", '["café","😀","\\u0000"]', "[null]",
             '[{"name":"first","amount":12.75,"tags":["a","b"]}]',
             '["' + "x" * (16384 - 4) + '"]']:
    check(2, 0, data, "null")
check(2, 5, "null", '"Erreur: café — \\ud83d\\ude00"')

# The evaluator's private list-of-alternatives format is not a public result item.
invalid = b'{"task_type":"RETRIEVE","status":"SUCCESS","retrieved_data":[["alternative"]]}'
try:
    public.FinalAgentResponse.model_validate_json(invalid)
except pydantic.ValidationError:
    pass
else:
    raise AssertionError("public schema unexpectedly accepts private alternative lists")
assert api.wfn_reset(C.byref(state)) == 0
reply = Reply(2, 0, b'[["alternative"]]', 17, b"null", 4)
assert api.wfn_submit(C.byref(state), C.byref(reply)) < 0
assert api.wfn_expire(C.byref(state)) == 0
assert api.wfn_json(C.byref(state), None, 0, None) < 0

# The runner records JSON null when the policy never submits. Exercise the
# unchanged official response evaluator with synthetic expectations, so absent
# episodes stay failures in a score denominator instead of missing files.
from types import SimpleNamespace
import webarena_verified.core.evaluation.evaluators.agent_response_evaluator as scorer_module
from webarena_verified.types.task import AgentResponseEvaluatorCfg
from webarena_verified.types.eval import EvalStatus

scorer_sources = [
    "core/evaluation/evaluators/agent_response_evaluator.py",
    "core/evaluation/evaluators/base.py",
    "core/evaluation/value_comparator.py",
    "core/evaluation/value_normalizer.py",
    "types/task.py", "types/eval.py", "types/agent_response.py",
]
scorer_source_hashes = {}
installed_scorer = Path(scorer_module.__file__).resolve().parents[3]
for relative in scorer_sources:
    file = installed_scorer / relative
    pinned = subprocess.check_output(["git", "-C", str(REFERENCE), "show",
                                     f"{manifest['revision']}:src/webarena_verified/{relative}"])
    assert file.read_bytes() == pinned, f"scorer source differs from pin: {relative}"
    scorer_source_hashes[relative] = hashlib.sha256(pinned).hexdigest()
scorer = scorer_module.AgentResponseEvaluator()
for kind in KINDS:
    expected = {"task_type": kind, "status": "SUCCESS", "retrieved_data": None}
    cfg = AgentResponseEvaluatorCfg(expected=expected, results_schema={"type": "array", "items": {"type": "string"}})
    ctx = SimpleNamespace(task=SimpleNamespace(task_id="synthetic-absence", is_retrieve_task=kind == "RETRIEVE", sites=[]),
                          agent_response_raw="null\n")
    assert scorer.evaluate(context=ctx, config=cfg).status == EvalStatus.FAILURE
    ctx.agent_response_raw = json.dumps(expected)
    assert scorer.evaluate(context=ctx, config=cfg).status == EvalStatus.SUCCESS

report = {
    "completed_at": datetime.now(timezone.utc).isoformat(),
    "reference_revision": manifest["revision"], "public_model": "FinalAgentResponse",
    "public_source_sha256": hashlib.sha256(expected_source).hexdigest(),
    "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
    "form_library_sha256": hashlib.sha256(form_library.read_bytes()).hexdigest(),
    "test_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "public_types_sha256": hashlib.sha256((Path(__file__).parent / "public_types.py").read_bytes()).hexdigest(),
    "pydantic_version": pydantic.__version__, "valid_submissions_checked": checked,
    "public_form_submissions_checked": checked,
    "private_alternatives_rejected": True, "budget_expiry_has_no_response": True,
    "official_null_response_failures_checked": len(KINDS),
    "official_response_positive_controls_checked": len(KINDS),
    "scorer_source_sha256": scorer_source_hashes,
    "browser_compared": False, "learned_policy_evaluated": False,
}
(library.parent / "schema-validation.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"PASS: {checked} direct and {checked} public-form schema cases; 3 null-response failures and 3 positive controls in the pinned official response evaluator")
