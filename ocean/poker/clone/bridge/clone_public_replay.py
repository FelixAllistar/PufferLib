"""Prospective public replay: no HTTP, model lookup, terminal holdings or payoff.

The backend uses this package's original-a4b native entry plus public-only
exports. This parser is a new source copy of the public grammar helper; it
imports no retained context or solver. It must receive its own frozen proof.
"""
from __future__ import annotations
import ctypes as C
from dataclasses import dataclass

I32_MAX = (1 << 31) - 1
U64_MAX = (1 << 64) - 1
RANKS, SUITS = "23456789TJQKA", "cdhs"

class ProtocolError(ValueError):
    pass

def integer(value, name, low=0, high=U64_MAX):
    if type(value) is not int or not low <= value <= high:
        raise ProtocolError(f"invalid strict integer {name}")
    return value

def cards(values, name, counts):
    if type(values) is not list or len(values) not in counts:
        raise ProtocolError(f"invalid {name} card count")
    result=[]
    for value in values:
        if type(value) is not str or len(value)!=2 or value[0] not in RANKS or value[1] not in SUITS:
            raise ProtocolError(f"invalid {name} card token")
        result.append(4*RANKS.index(value[0])+SUITS.index(value[1]))
    if len(set(result))!=len(result):
        raise ProtocolError(f"duplicate {name} card")
    return tuple(result)

class Public(C.Structure):
    _fields_ = [(name, C.c_int64) for name in (
        "actor", "street", "button", "decisions", "pot", "high", "increment",
        "live", "active", "pending", "raises", "last_raiser", "board_count")]
    _fields_ += [("board", C.c_int64 * 5), ("line", C.c_int64 * 2)]
    _fields_ += [(name, C.c_int64) for name in (
        "stack0", "initial0", "total0", "bet0", "reopen0",
        "stack1", "initial1", "total1", "bet1", "reopen1", "chips_per_bb")]

@dataclass(frozen=True)
class Event:
    before: bytes
    action: int
    after: bytes

def public_from_native(native):
    result=Public()
    if not native.lib.bb_public_from_game(native.storage,0,C.byref(result)):
        raise ProtocolError("public snapshot invalid or already settled")
    return result

def bind_public_library(lib):
    lib.bb_public_from_game.argtypes=[C.c_void_p,C.c_int,C.POINTER(Public)]
    lib.bb_public_from_game.restype=C.c_int
    lib.bb_public_next.argtypes=[C.POINTER(Public),C.c_int32,C.POINTER(C.c_int64),C.c_int,C.POINTER(Public)]
    lib.bb_public_next.restype=C.c_int
    lib.bb_game_status.argtypes=[C.c_void_p,C.c_int,C.POINTER(C.c_int64)]
    lib.bb_game_status.restype=C.c_int

def _status(native):
    out=(C.c_int64*2)()
    if not native.lib.bb_game_status(native.storage,0,out):
        raise ProtocolError("native public status unavailable")
    return tuple(out)

def _validate_action(state, token, action):
    if type(token) is not str or token not in ("b","k","c","f"):
        raise ProtocolError("invalid action token kind")
    integer(action,"native action",-1,I32_MAX)
    if ((token=="f" and action!=-1) or (token in ("k","c") and action!=0)
            or (token=="b" and action<=0)):
        raise ProtocolError("action token/value mismatch")
    actor = state.actor
    if actor not in (0,1):raise ProtocolError("action requires a live actor")
    bet = getattr(state, f"bet{actor}")
    stack = getattr(state, f"stack{actor}")
    call = state.high - bet
    if token == "k" and call != 0:
        raise ProtocolError("check while facing a wager")
    if token == "c" and call <= 0:
        raise ProtocolError("call without a wager")
    if action == -1:
        if call <= 0:
            raise ProtocolError("fold without a wager")
        return
    if action == 0:
        return
    other = 1 - actor
    maximum = bet + stack
    can_raise = (state.high >= getattr(state, f"reopen{actor}") and maximum > state.high
                 and state.active & (1 << other)
                 and getattr(state, f"bet{other}") + getattr(state, f"stack{other}") > state.high)
    if (not can_raise or action <= state.high or action > maximum
            or action < state.high + state.increment and action != maximum):
        raise ProtocolError("illegal street raise-to")


def _make_replay(native, hero, own, visible):
    # Future cards and opponent cards exist only in the replay engine. They are
    # arbitrary fillers chosen without any terminal revealed opponent holding.
    available = iter(c for c in range(52) if c not in own + visible)
    opponent = (next(available), next(available))
    board = visible + tuple(next(available) for _ in range(5 - len(visible)))
    holes = opponent + own if hero else own + opponent
    replay = (native.fork_replay() if hasattr(native,"fork_replay") else
              type(native)(players=2,seed=1,library=native.path,samples=native.samples))
    bind_public_library(replay.lib)
    replay.reset([20000, 20000], holes + board, button=1)
    return replay, board


def _rebuild(native, hero, own, visible, history):
    replay, board = _make_replay(native, hero, own, visible)
    initial = public_from_native(replay)
    state = initial
    events = []
    i = 0
    while i < len(history):
        if state.actor == 255:
            raise ProtocolError("action after terminal")
        token = history[i]
        i += 1
        if token == "b":
            first = i
            while i < len(history) and "0" <= history[i] <= "9":
                i += 1
            if i == first or i - first > 10:
                raise ProtocolError("invalid wager encoding")
            action = integer(int(history[first:i]), "wager", 1, I32_MAX)
        elif token in "kcf":
            action = -1 if token == "f" else 0
        else:
            raise ProtocolError("unexpected action character or street separator")
        _validate_action(state, token, action)
        before = state
        replay.step(action)
        engine = _status(replay)
        count = engine[1] + 2 if engine[1] else 0
        state = Public()
        if not replay.lib.bb_public_next(C.byref(before), action, (C.c_int64 * 5)(*board), count, C.byref(state)):
            raise ProtocolError("native/public transition disagreement")
        if (engine[0] == 255) != (state.actor == 255):
            raise ProtocolError("native/public terminal status differs")
        if engine[0] != 255 and bytes(state) != bytes(public_from_native(replay)):
            raise ProtocolError("native/public state disagreement")
        events.append(Event(bytes(before), action, bytes(state)))
        if state.actor == 255:
            # Official sample_api.py permits zero slashes or all remaining
            # pre-river street separators after an all-in call. No partial set.
            remaining = 3 - before.street
            allowed = ("", "/" * remaining) if (token == "c" and state.live == 3 and before.street < 3) else ("",)
            if history[i:] not in allowed:
                raise ProtocolError("invalid terminal street-separator suffix")
            i = len(history)
        elif state.street != before.street and i < len(history):
            # A final separator is optional; it is required before more action.
            if history[i] != "/":
                raise ProtocolError("missing street separator")
            i += 1
    if len(visible) != state.board_count or tuple(state.board[:len(visible)]) != visible:
        raise ProtocolError("visible board does not match action-prefix street")
    return replay, initial, state, tuple(events)

