"""ctypes declarations for browser-context qualification, outside runtime loops."""
import ctypes as C
from pathlib import Path
from public_types import U32, View, Capabilities, Action

ROOT = Path(__file__).resolve().parents[3]
LIBRARY = ROOT / "build/webnav/primitives/browser_contexts/libbrowser_contexts.so"


class State(C.Structure):
    _fields_ = [("words", U32 * 2048)]


class BrowserView(C.Structure):
    _fields_ = [(key, U32) for key in ("route", "phase", "can_back", "can_forward")]


class TabView(C.Structure):
    _fields_ = [("identity", U32), ("browser", BrowserView)]


class ContextView(C.Structure):
    _fields_ = [(key, U32) for key in ("count", "active", "can_open", "can_close")] + [("tabs", TabView * 16)]


class Session(C.Structure):
    _fields_ = [("ref_base", U32), ("elapsed_ms", U32), ("browser", State)]


class Command(C.Structure):
    _fields_ = [(key, U32) for key in ("kind", "argument", "foreground", "elapsed_ms")]


def load():
    lib = C.CDLL(str(LIBRARY))
    signatures = {
        "wbc_reset": [C.POINTER(State), U32, U32, U32],
        "wbc_step": [C.POINTER(State), U32, U32, U32, U32],
        "wbc_observe": [C.POINTER(State), C.POINTER(ContextView)],
        "wbc_session_reset": [C.POINTER(Session), U32, U32, U32, U32],
        "wbc_session_command": [C.POINTER(Session), C.POINTER(Command)],
        "wbc_session_action": [C.POINTER(Session), C.POINTER(Action)],
        "wbc_session_observe": [C.POINTER(Session), C.POINTER(C.c_char_p), C.c_char_p, C.c_char_p,
                                C.POINTER(View), C.POINTER(Capabilities)],
    }
    for name, args in signatures.items():
        getattr(lib, name).argtypes = args
        getattr(lib, name).restype = C.c_int
    return lib
