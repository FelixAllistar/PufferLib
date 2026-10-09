"""ctypes for native public-interface qualification tests, outside runtime loops."""
import ctypes as C

U32 = C.c_uint32


class Text(C.Structure):
    _fields_ = [("offset", U32), ("length", U32)]


class Node(C.Structure):
    _fields_ = [(name, U32) for name in ("ref", "parent", "role", "flags")] + [
        ("name", Text), ("value", Text),
    ] + [(name, C.c_float) for name in ("x", "y", "width", "height")] + [
        (name, U32) for name in ("selection_start", "selection_end", "capacity")
    ] + [(name, C.c_float) for name in ("scroll_x", "scroll_y", "scroll_max_x", "scroll_max_y")]


class View(C.Structure):
    _fields_ = [(name, U32) for name in ("version", "count", "text_bytes", "omitted",
                                       "text_truncated", "elapsed_ms", "deadline_ms")] + [
        ("instruction", Text), ("nodes", Node * 128), ("text", C.c_char * 16384),
    ]


class Capability(C.Structure):
    _fields_ = [(name, U32) for name in ("kind", "ref", "wire_target", "flags", "text_capacity",
                                       "min0", "max0", "step0", "unit0", "min1", "max1", "step1", "unit1")]


class Capabilities(C.Structure):
    _fields_ = [("version", U32), ("count", U32), ("incomplete", U32), ("items", Capability * 1024)]


class Action(C.Structure):
    _fields_ = [(name, U32) for name in ("kind", "target", "arg0", "arg1", "elapsed_ms")] + [
        ("text", C.c_char_p), ("text_length", C.c_size_t),
    ]


def text(view, span):
    return C.string_at(C.addressof(view) + View.text.offset + span.offset, span.length).decode("utf-8")


def snapshot(view, caps, deadline_ms):
    result = {"version": view.version, "instruction": text(view, view.instruction),
              "elapsed_ms": view.elapsed_ms, "deadline_ms": deadline_ms,
              "omitted": view.omitted, "text_truncated": view.text_truncated,
              "incomplete": caps.incomplete, "nodes": [], "capabilities": []}
    for node in view.nodes[:view.count]:
        result["nodes"].append({name: text(view, getattr(node, name)) if kind is Text else getattr(node, name)
                                for name, kind in Node._fields_})
    for cap in caps.items[:caps.count]:
        result["capabilities"].append({name: getattr(cap, name) for name, _ in Capability._fields_})
    return result
