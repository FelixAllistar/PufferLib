"""Drive public browser-shell controls with the unchanged shared policy.

This qualification driver supplies public starting documents and no private
goals, rewards, expected outputs or success signals. It is not a training loop.
"""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import selectors
import subprocess
from public_types import View, Capabilities, Action, snapshot
from browser_contexts_types import ROOT, LIBRARY, Session, ContextView, Command, load

api = load()
titles = (b"Blank", b"Records", b"Orders", b"Reports")
urls = (b"about:blank", b"https://example.test/records", b"https://example.test/orders", b"https://example.test/reports")


def observe(state):
    public = ContextView()
    assert api.wbc_observe(C.byref(state.browser), C.byref(public)) == 0
    labels = (C.c_char_p * 16)()
    address = None
    for i, tab in enumerate(public.tabs[:public.count]):
        labels[i] = titles[tab.browser.route]
        if tab.identity == public.active:
            address = urls[tab.browser.route]
    view, caps = View(), Capabilities()
    assert api.wbc_session_observe(C.byref(state), labels, address,
                                  b"Inspect the open documents using the browser controls.",
                                  C.byref(view), C.byref(caps)) == 0
    return snapshot(view, caps, 120000)


def request(process, selector, value):
    process.stdin.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n")
    process.stdin.flush()
    assert selector.select(timeout=15), "policy RPC response timed out"
    line = process.stdout.readline()
    assert line, "policy RPC closed without response"
    result = json.loads(line)
    assert "error" not in result, result
    return result


binary = ROOT / "build/webnav/benchmarks/policy_rpc"
results = []
for policy in ("random", "checkpoints/webnav_unified/1790999586398/0000000000998400.bin"):
    process = subprocess.Popen([str(binary), policy, "--sample"], cwd=ROOT,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    counts = {"policy": policy, "episodes": 4, "choices": 0, "public_actions": 0,
              "local_choices": 0, "action_counts": {}}
    try:
        for trial, seed in enumerate((7, 29, 101, 997)):
            state = Session()
            assert api.wbc_session_reset(C.byref(state), seed, 4, 16, 4096 + trial * 65536) == 0
            # Equivalent to a host's public initial URLs, chosen without goals.
            assert api.wbc_session_command(C.byref(state), C.byref(Command(1, 1 + seed % 3, 0, 0))) == 0
            assert api.wbc_session_command(C.byref(state), C.byref(Command(6, 1 + (seed + 1) % 3, 0, 0))) == 0
            assert request(process, selector, {"reset": True, "seed": seed}) == {"reset": True}
            for _ in range(64):
                view = observe(state)
                command = request(process, selector, view)
                counts["choices"] += 1
                if command["local"]:
                    counts["local_choices"] += 1
                    continue
                payload = command["text"].encode("utf-8")
                action = Action(command["kind"], command["target"], command["arg0"], command["arg1"],
                                50, payload, len(payload))
                assert api.wbc_session_action(C.byref(state), C.byref(action)) == 0, (command, view)
                counts["public_actions"] += 1
                label = next((n["name"] for n in view["nodes"] if n["ref"] == command["target"]), "Wait")
                counts["action_counts"][label] = counts["action_counts"].get(label, 0) + 1
        assert counts["public_actions"] > 0
    finally:
        selector.close()
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

files = [LIBRARY, binary, Path(__file__), Path(__file__).with_name("browser_contexts_types.py"), Path(__file__).with_name("public_types.py")]
report = {"completed_at": datetime.now(timezone.utc).isoformat(),
          "sha256": {str(file.relative_to(ROOT)): hashlib.sha256(file.read_bytes()).hexdigest() for file in files},
          "purpose": "public shell interface qualification; no rewards or success scores", "results": results}
(LIBRARY.parent / "policy-validation.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"PASS: unchanged random/checkpoint policies drive 8 native browser-control episodes through {sum(r['choices'] for r in results)} choices")
