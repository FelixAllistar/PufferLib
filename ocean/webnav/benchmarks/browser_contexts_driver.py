"""Test-only ctypes bridge for independent Chromium/context comparisons.

This process never runs in the simulator, learner or browser policy adapter.
Only public tab/navigation facts are returned; private words stay in this host.
"""
import ctypes as C
import json
import sys
from browser_contexts_types import State, BrowserView, ContextView, load

lib = load()
state = State()
for line in sys.stdin:
    try:
        request = json.loads(line)
        assert isinstance(request, dict) and len(request) == 1
        name, args = next(iter(request.items()))
        assert isinstance(args, list) and all(type(n) is int and 0 <= n <= 0xFFFFFFFF for n in args)
        assert (name == "reset" and len(args) == 3) or (name == "step" and len(args) == 4)
        before = bytes(state)
        result = getattr(lib, "wbc_" + name)(C.byref(state), *args)
        if result:
            assert bytes(state) == before
            print(json.dumps({"error": "native command rejected atomically"}), flush=True)
            continue
        view = ContextView()
        assert lib.wbc_observe(C.byref(state), C.byref(view)) == 0
        print(json.dumps({"active": view.active, "can_open": view.can_open,
                          "can_close": view.can_close, "tabs": [
                              {"identity": tab.identity, **{key: getattr(tab.browser, key)
                                for key, _ in BrowserView._fields_}}
                              for tab in view.tabs[:view.count]]}), flush=True)
    except Exception as error:
        print(json.dumps({"error": str(error) or type(error).__name__}), flush=True)
