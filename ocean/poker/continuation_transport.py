"""New retained-river transport prototype; no HTTP client or automatic retry.

Context accepts the existing eval.Native object. Session validates a complete
public prefix on a separate dummy-hole engine before forwarding new events.
The separate generation argument must be captured BEFORE sending a request;
the receiving caller must never substitute its current generation afterward.

Terminal policy events contain unsettled public money. The dummy engine's
winner payout is ignored. Revealed-card settlement is a separate operation
permitted only after the native context has closed.
"""
from __future__ import annotations
import ctypes as C
import hashlib
from dataclasses import dataclass

U64_MAX = (1 << 64) - 1
I32_MAX = (1 << 31) - 1
RANKS, SUITS = "23456789TJQKA", "cdhs"


class ProtocolError(ValueError):
    pass


def integer(value, name, low=0, high=U64_MAX):
    if type(value) is not int or not low <= value <= high:
        raise ProtocolError(f"{name} must be a strict integer in [{low}, {high}]")
    return value


def cards(values, name, counts):
    if type(values) is not list or len(values) not in counts:
        raise ProtocolError(f"{name} has invalid card count")
    result = []
    for value in values:
        if (type(value) is not str or len(value) != 2
                or value[0] not in RANKS or value[1] not in SUITS):
            raise ProtocolError(f"{name} has an invalid card")
        result.append(4 * RANKS.index(value[0]) + SUITS.index(value[1]))
    if len(set(result)) != len(result):
        raise ProtocolError(f"{name} repeats a card")
    return tuple(result)


class Public(C.Structure):
    _fields_ = [(name, C.c_int64) for name in (
        "actor", "street", "button", "decisions", "pot", "high", "increment",
        "live", "active", "pending", "raises", "last_raiser", "board_count")]
    _fields_ += [("board", C.c_int64 * 5), ("line", C.c_int64 * 2)]
    _fields_ += [(name, C.c_int64) for name in (
        "stack0", "initial0", "total0", "bet0", "reopen0",
        "stack1", "initial1", "total1", "bet1", "reopen1", "chips_per_bb")]

    def copy(self):
        return Public.from_buffer_copy(bytes(self))


class Audit(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in (
        "generation", "events", "requests", "creates", "iterate_calls", "iterations",
        "pending_request", "pending_rng_before", "pending_rng_after", "anchors", "hits", "fallbacks")]
    _fields_ += [(name, C.c_int32) for name in (
        "phase", "seat", "node", "checkpoint", "pending", "pending_action", "solve_status")]
    _fields_ += [(name, C.c_ubyte * 32) for name in (
        "owner", "public_hash", "prefix_hash", "anchor_hash", "strategy_hash", "whole_hash")]


@dataclass(frozen=True)
class Event:
    before: bytes
    action: int
    after: bytes


def bind_library(lib):
    owner = C.POINTER(C.c_ubyte)
    public = C.POINTER(Public)
    args = {
        "pc_create": [owner, C.c_int],
        "pc_free": [C.c_void_p],
        "pc_begin": [C.c_void_p, owner, C.c_uint64, C.c_int, C.c_int],
        "pc_envelope": [C.c_void_p, owner, C.c_uint64],
        "pc_closed_envelope": [C.c_void_p, owner, C.c_uint64],
        "pc_closed_receipt": [C.c_void_p, owner, C.c_uint64, public, C.c_int, C.c_int],
        "pc_start": [C.c_void_p, owner, C.c_uint64, public],
        "pc_own": [C.c_void_p, owner, C.c_uint64, C.c_int, C.c_int],
        "pc_observe": [C.c_void_p, owner, C.c_uint64, public, C.c_int32, public],
        "pc_decide": [C.c_void_p, owner, C.c_uint64, C.c_void_p, C.c_int,
                      C.POINTER(C.c_uint64), C.c_void_p, C.c_uint64, C.POINTER(C.c_int32)],
        "pc_end": [C.c_void_p, owner, C.c_uint64, C.c_int],
        "pc_protocol_error": [C.c_void_p, C.c_char_p],
        "pc_audit": [C.c_void_p, C.POINTER(Audit)],
        "pc_public_from_game": [C.c_void_p, C.c_int, public],
        "pc_public_next": [public, C.c_int32, C.POINTER(C.c_int64), C.c_int, public],
        "pc_public_transition": [public, C.c_int32, public],
        "pc_last_error": [C.c_void_p],
    }
    for name, signature in args.items():
        function = getattr(lib, name)
        function.argtypes = signature
        function.restype = C.c_int
    lib.pc_create.restype = C.c_void_p
    lib.pc_free.restype = None
    lib.pc_last_error.restype = C.c_char_p


class Context:
    def __init__(self, native, owner: str, seat: int, policy=7, samples=0):
        if type(owner) is not str or not owner or len(owner.encode("utf-8")) > 512:
            raise ProtocolError("owner must be a nonempty bounded namespace")
        self.seat = integer(seat, "seat", 0, 1)
        self.policy = integer(policy, "policy", 0, 7)
        self.samples = integer(samples, "samples", 0, 128)
        if self.samples not in (0, 32, 64, 128):
            raise ProtocolError("unsupported sample budget")
        if native.players != 2:
            raise ProtocolError("retained context is heads-up only")
        self.native, self.lib = native, native.lib
        bind_library(self.lib)
        self.owner = (C.c_ubyte * 32).from_buffer_copy(hashlib.sha256(owner.encode("utf-8")).digest())
        self.ptr = self.lib.pc_create(self.owner, self.seat)
        if not self.ptr:
            raise MemoryError("could not allocate river context")
        self.generation, self.closed = 0, True
        self.started = False

    def _check(self, result):
        if not result:
            detail = self.lib.pc_last_error(self.ptr).decode("utf-8", "replace")
            raise ProtocolError(detail or "native context rejected operation")

    def protocol_error(self, message):
        if self.ptr:
            self.lib.pc_protocol_error(self.ptr, str(message).encode("utf-8")[:150])

    def begin(self, generation):
        try:
            generation = integer(generation, "generation", 1)
            self._check(self.lib.pc_begin(self.ptr, self.owner, generation, self.policy, self.samples))
        except Exception as exc:
            self.protocol_error(exc)
            raise
        self.generation, self.closed, self.started = generation, False, False

    def envelope(self, generation, closed_terminal=False):
        try:
            generation = integer(generation, "generation", 1)
            function = self.lib.pc_closed_envelope if closed_terminal else self.lib.pc_envelope
            self._check(function(self.ptr, self.owner, generation))
        except Exception as exc:
            self.protocol_error(exc)
            raise

    def own(self, own):
        try:
            if len(own) != 2:
                raise ProtocolError("own holding must have two cards")
            a = integer(own[0], "own card", 0, 51)
            b = integer(own[1], "own card", 0, 51)
            self._check(self.lib.pc_own(self.ptr, self.owner, self.generation, a, b))
        except Exception as exc:
            self.protocol_error(exc)
            raise

    def start(self, state):
        try:
            self._check(self.lib.pc_start(self.ptr, self.owner, self.generation, C.byref(state)))
            self.started = True
        except Exception as exc:
            self.protocol_error(exc)
            raise

    def observe(self, before, action, after):
        try:
            action = integer(action, "action", -1, I32_MAX)
            self._check(self.lib.pc_observe(self.ptr, self.owner, self.generation,
                                           C.byref(before), action, C.byref(after)))
        except Exception as exc:
            self.protocol_error(exc)
            raise

    def decide(self, request_id):
        try:
            request_id = integer(request_id, "request ID", 1)
            result = C.c_int32()
            self._check(self.lib.pc_decide(self.ptr, self.owner, self.generation,
                self.native.storage, 0, C.byref(self.native.rng), None, request_id, C.byref(result)))
            return result.value
        except Exception as exc:
            self.protocol_error(exc)
            raise

    def end(self, reason=1):
        try:
            reason = integer(reason, "end reason", 1, 3)
            self._check(self.lib.pc_end(self.ptr, self.owner, self.generation, reason))
            self.closed = True
        except Exception as exc:
            self.protocol_error(exc)
            raise

    def abandon(self):
        """Close an uncertain HTTP operation. Its wager must not be retried."""
        if not self.closed:
            audit = self.audit()
            if audit["phase"] == 5:
                # A latched protocol error is already unusable. Keep its audit;
                # pc_free or the next strictly increasing begin releases memory.
                self.closed = True
            else:
                self.end(2)

    def audit(self):
        result = Audit()
        self._check(self.lib.pc_audit(self.ptr, C.byref(result)))
        return {name: (bytes(getattr(result, name)).hex() if isinstance(getattr(result, name), C.Array)
                       else getattr(result, name)) for name, _ in Audit._fields_}

    def close(self):
        if self.ptr:
            self.lib.pc_free(self.ptr)
            self.ptr = None
        self.closed = True

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def public_from_native(native):
    result = Public()
    if not native.lib.pc_public_from_game(native.storage, 0, C.byref(result)):
        raise ProtocolError("native public snapshot invalid or already settled")
    return result


def _validate_action(state, token, action):
    actor = state.actor
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
    replay = type(native)(players=2, seed=1, library=native.path, samples=native.samples)
    bind_library(replay.lib)
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
        engine = replay.snapshot()[0]
        count = engine[1] + 2 if engine[1] else 0
        state = Public()
        if not replay.lib.pc_public_next(C.byref(before), action, (C.c_int64 * 5)(*board), count, C.byref(state)):
            raise ProtocolError("native/public transition disagreement")
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


class Session:
    def __init__(self, context: Context):
        self.context = context
        self.generation = 0
        self.events = ()
        self.board = ()
        self.own = None
        self.terminal = False
        self.last_status = None

    @property
    def native(self):
        return self.context.native

    def ingest(self, generation, response):
        """Validate and ingest a response captured under the supplied envelope.

        Returns a dict with terminal/closed/events/new_events and native audit.
        A terminal result closes policy context BEFORE any separate settlement.
        Repeated nonterminal semantic prefixes leave context and RNG unchanged.
        """
        ctx = self.context
        try:
            # This MUST precede even looking at response's type/cards/actions.
            duplicate_terminal = self.terminal and self.generation == ctx.generation
            ctx.envelope(generation, closed_terminal=duplicate_terminal)
            if self.generation != generation:
                if ctx.started:
                    raise ProtocolError("session lost its live public prefix")
                self.generation, self.events, self.board, self.own = generation, (), (), None
                self.terminal, self.last_status = False, None
            if type(response) is not dict:
                raise ProtocolError("response must be a JSON object")
            if "error_msg" in response:
                raise ProtocolError("server returned an error response")
            hero = response.get("client_pos", response.get("position"))
            hero = integer(hero, "client position", 0, 1)
            if "client_pos" in response and "position" in response:
                if integer(response["position"], "position alias", 0, 1) != hero:
                    raise ProtocolError("position aliases conflict")
            if hero != ctx.seat:
                raise ProtocolError("response belongs to another seat")
            own = tuple(sorted(cards(response.get("hole_cards"), "own holding", (2,))))
            visible = cards(response.get("board"), "visible board", (0, 3, 4, 5))
            if set(own).intersection(visible):
                raise ProtocolError("own holding overlaps visible board")
            if self.own is not None and own != self.own:
                raise ProtocolError("own holding changed within hand")
            if visible[:len(self.board)] != self.board:
                raise ProtocolError("visible board is not a prefix extension")
            history = response.get("action")
            if type(history) is not str or len(history) > 65535:
                raise ProtocolError("invalid action history")
            replay, initial, state, events = _rebuild(ctx.native, hero, own, visible, history)
            if len(events) < len(self.events) or events[:len(self.events)] != self.events:
                raise ProtocolError("ordered public history was replaced or truncated")
            terminal = state.actor == 255
            if "terminal" in response:
                marker = response["terminal"]
                if type(marker) is not bool:
                    raise ProtocolError("terminal marker must be a strict boolean")
                if "winnings" in response and not marker:
                    raise ProtocolError("terminal marker conflicts with payoff presence")
            else:
                # Direct official-response compatibility checks presence only;
                # no payoff value is read before the policy context closes.
                marker = "winnings" in response
            if terminal != marker:
                raise ProtocolError("terminal marker contradicts public action history")
            if not terminal and state.actor != hero:
                raise ProtocolError("response does not stop at the client's turn")
            if duplicate_terminal:
                if not terminal or events != self.events:
                    raise ProtocolError("closed terminal prefix changed")
                ctx._check(ctx.lib.pc_closed_receipt(ctx.ptr, ctx.owner, generation,
                    C.byref(state), own[0], own[1]))
                return {"terminal": True, "closed": True, "events": len(events),
                        "new_events": 0, "audit": ctx.audit()}
            # Every card and the entire prefix were checked before mutation.
            # This binding is repeated for terminal responses as well.
            ctx.own(own)
            if not ctx.started:
                ctx.start(initial)
            new_events = events[len(self.events):]
            for event in new_events:
                ctx.observe(Public.from_buffer_copy(event.before), event.action,
                            Public.from_buffer_copy(event.after))
            C.memmove(ctx.native.storage, replay.storage, len(ctx.native.storage))
            self.events, self.board, self.own, self.terminal = events, visible, own, terminal
            if terminal:
                ctx.end(1)
            self.last_status = {"terminal": terminal, "closed": ctx.closed,
                                "events": len(events), "new_events": len(new_events),
                                "audit": ctx.audit()}
            return self.last_status
        except Exception as exc:
            ctx.protocol_error(exc)
            raise

    def decide(self, request_id):
        if self.terminal:
            self.context.protocol_error("decision after terminal")
            raise ProtocolError("decision after terminal")
        return self.context.decide(request_id)

    def abandon(self):
        self.context.abandon()

    def settle_revealed(self, generation, response):
        """Independent native settlement AFTER policy close, if cards revealed.

        Returns None for a mucked holding. This does not change policy context,
        call its decision method, or place actual opponent cards in its engine.
        """
        integer(generation, "generation", 1)
        if (generation != self.generation or not self.terminal
                or not self.context.closed or self.context.audit()["phase"] != 4):
            raise ProtocolError("revealed settlement requires closed terminal generation")
        hero = integer(response.get("client_pos", response.get("position")), "settlement position", 0, 1)
        if hero != self.context.seat:
            raise ProtocolError("settlement belongs to another seat")
        if "client_pos" in response and "position" in response:
            if integer(response["position"], "settlement position alias", 0, 1) != hero:
                raise ProtocolError("settlement position aliases conflict")
        own = tuple(sorted(cards(response.get("hole_cards"), "own holding", (2,))))
        visible = cards(response.get("board"), "visible board", (0, 3, 4, 5))
        if own != self.own or visible != self.board:
            raise ProtocolError("settlement response changed policy-known cards")
        if "slumbot_hole_cards" in response and "bot_hole_cards" in response:
            if response["slumbot_hole_cards"] != response["bot_hole_cards"]:
                raise ProtocolError("revealed holding aliases conflict")
        other_value = response.get("slumbot_hole_cards", response.get("bot_hole_cards"))
        if other_value is None or type(other_value) is list and not other_value:
            return None
        other = cards(other_value, "revealed opponent holding", (2,))
        if set(other).intersection(own + visible):
            raise ProtocolError("revealed opponent holding overlaps known cards")
        history = response.get("action")
        replay, _, _, events = _rebuild(self.native, self.context.seat, own, visible, history)
        if events != self.events:
            raise ProtocolError("settlement action history changed")
        available = iter(c for c in range(52) if c not in own + other + visible)
        board = visible + tuple(next(available) for _ in range(5-len(visible)))
        holes = other + own if self.context.seat else own + other
        replay.reset([20000,20000], holes + board, button=1)
        for event in events:
            replay.step(event.action)
        state, rows, _ = replay.snapshot()
        if state[0] != 255:
            raise ProtocolError("revealed settlement not terminal")
        winnings = integer(response.get("winnings"), "winnings", -20000, 20000)
        actual = rows[self.context.seat][0] - 20000
        if actual != winnings:
            raise ProtocolError("revealed native/server settlement disagrees")
        return {"verified": True, "winnings_chips": actual}
