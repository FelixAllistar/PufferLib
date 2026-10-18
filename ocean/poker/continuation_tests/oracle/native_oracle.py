"""New bounded continuation recovery proof, independent of transport policy code.

No executable work is done on import. A sealed runner supplies three libraries:
the frozen V5 reference, the production candidate, and an explicitly proof-only
instrumented candidate. Six public fixtures are newly authored, not recovered
old proof. Full all-node averaged bytes are compared to frozen V5; public
transitions and canonical digest encodings are independently implemented here.
"""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import traceback

ROOT = Path(__file__).resolve().parent
MASK = (1 << 64) - 1
HANDS = 1081
FIXTURE_SHA = "ecacc4a32ceaf7070be46ba4c59d17d69587ce1cc999ccf53f8bf061ce80353d"
CONTROL_SHA = "b74b44f37a33736c685a2da68d3815f25a65c2dee0fc9cebdfcd7b6cbd11af4a"
U64 = C.c_uint64
I64 = C.c_int64
I32 = C.c_int32
Ptr = C.c_void_p
Owner = C.c_ubyte * 32


class Public(C.Structure):
    _fields_ = [("v", I64 * 31)]

    @classmethod
    def of(cls, values):
        assert len(values) == 31 and all(type(v) is int for v in values)
        return cls((I64 * 31)(*values))


class RPublic(C.Structure):
    _fields_ = [("pot", I64), ("total", I64 * 2), ("bet", I64 * 2),
                ("stack", I64 * 2), ("high", I64), ("increment", I64),
                ("reopen", I64 * 2), ("pending", C.c_int),
                ("actor", C.c_int), ("chips_per_bb", C.c_double)]


class Hand(C.Structure):
    _fields_ = [("c0", C.c_uint8), ("c1", C.c_uint8), ("rank", C.c_uint32)]


class Inputs(C.Structure):
    _fields_ = [("state", RPublic), ("hand", Hand * HANDS),
                ("prior", (C.c_double * HANDS) * 2)]


class Node(C.Structure):
    _fields_ = [("state", RPublic), ("kind", C.c_int), ("folder", C.c_int),
                ("action_count", C.c_int), ("child", C.c_int * 3),
                ("action", I64 * 3), ("payoff0", C.c_double * 3)]


CERT_FLOATS = ("value_chips", "upper_chips", "lower_chips", "nashconv_chips",
    "gap_over_two_chips", "value_bb", "upper_bb", "lower_bb", "nashconv_bb",
    "gap_over_two_bb", "chance_normalizer")


class Certificate(C.Structure):
    _fields_ = [(name, C.c_double) for name in CERT_FLOATS] + [("iterations", U64)]


AUDIT_U64 = ("generation", "events", "requests", "creates", "iterate_calls", "iterations",
    "pending_request", "pending_rng_before", "pending_rng_after", "anchors", "hits", "fallbacks")
AUDIT_I32 = ("phase", "seat", "node", "checkpoint", "pending", "pending_action", "solve_status")
AUDIT_HASH = ("owner", "public_hash", "prefix_hash", "anchor_hash", "strategy_hash", "whole_hash")


class Audit(C.Structure):
    _fields_ = ([(name, U64) for name in AUDIT_U64]
        + [(name, I32) for name in AUDIT_I32]
        + [(name, Owner) for name in AUDIT_HASH])


class Counts(C.Structure):
    _fields_ = [(name, U64) for name in ("creates", "iterate_calls", "iterations",
        "certificate_calls", "external_draws", "row_draws", "last_row_before",
        "last_row_after", "last_row_return")]


def signature(lib, name, args, result=C.c_int):
    fn = getattr(lib, name)
    fn.argtypes, fn.restype = args, result


def bind_reference(path):
    lib = C.CDLL(str(path))
    for name in ("reference_native_size",):
        signature(lib, name, [], C.c_size_t)
    signature(lib, "reference_native_create", [C.POINTER(I64), C.POINTER(C.c_int)], Ptr)
    signature(lib, "reference_native_free", [Ptr], None)
    signature(lib, "reference_native_step", [Ptr, I32], None)
    signature(lib, "reference_unsettled", [Ptr, C.POINTER(I64)])
    signature(lib, "reference_create", [C.POINTER(I64)], Ptr)
    signature(lib, "reference_free", [Ptr], None)
    for name in ("reference_status", "reference_checkpoint", "reference_iterate_calls", "reference_node_count"):
        signature(lib, name, [Ptr])
    signature(lib, "reference_node", [Ptr, C.c_int, C.POINTER(Node)])
    signature(lib, "reference_average", [Ptr, C.c_int, C.POINTER(C.c_double), C.c_size_t])
    signature(lib, "reference_certificate", [Ptr, C.POINTER(Certificate)])
    signature(lib, "reference_inputs", [Ptr, C.POINTER(Inputs)])
    signature(lib, "poker_reset", [Ptr, C.c_int, C.c_int,
        C.POINTER(C.c_uint32), C.POINTER(C.c_uint8)], None)
    return lib


def bind_candidate(path, probe=False):
    lib = C.CDLL(str(path))
    op, pp = C.POINTER(C.c_ubyte), C.POINTER(Public)
    signatures = {
        "pc_create": ([op, C.c_int], Ptr), "pc_free": ([Ptr], None),
        "pc_begin": ([Ptr, op, U64, C.c_int, C.c_int], C.c_int),
        "pc_envelope": ([Ptr, op, U64], C.c_int),
        "pc_closed_envelope": ([Ptr, op, U64], C.c_int),
        "pc_closed_receipt": ([Ptr, op, U64, pp, C.c_int, C.c_int], C.c_int),
        "pc_start": ([Ptr, op, U64, pp], C.c_int),
        "pc_own": ([Ptr, op, U64, C.c_int, C.c_int], C.c_int),
        "pc_observe": ([Ptr, op, U64, pp, I32, pp], C.c_int),
        "pc_decide": ([Ptr, op, U64, Ptr, C.c_int, C.POINTER(U64), Ptr, U64, C.POINTER(I32)], C.c_int),
        "pc_end": ([Ptr, op, U64, C.c_int], C.c_int),
        "pc_protocol_error": ([Ptr, C.c_char_p], C.c_int),
        "pc_audit": ([Ptr, C.POINTER(Audit)], C.c_int),
        "pc_last_error": ([Ptr], C.c_char_p),
        "pc_certificate": ([Ptr, C.POINTER(Certificate)], C.c_int),
        "pc_node_count": ([Ptr], C.c_int), "pc_allocated_bytes": ([Ptr], C.c_size_t),
        "pc_node_info": ([Ptr, C.c_int, C.POINTER(Node)], C.c_int),
        "pc_average": ([Ptr, C.c_int, C.POINTER(C.c_double), C.c_size_t], C.c_int),
        "pc_public_from_game": ([Ptr, C.c_int, pp], C.c_int),
        "pc_public_next": ([pp, I32, C.POINTER(I64), C.c_int, pp], C.c_int),
        "pc_public_transition": ([pp, I32, pp], C.c_int),
    }
    for name, (args, result) in signatures.items():
        signature(lib, name, args, result)
    if probe:
        signature(lib, "probe_reset", [], None)
        signature(lib, "probe_fault", [C.c_int], None)
        signature(lib, "probe_force_local", [C.c_int, C.c_uint32], None)
        signature(lib, "probe_read_counts", [C.POINTER(Counts)], None)
        signature(lib, "probe_decide", signatures["pc_decide"][0])
        signature(lib, "pc_test_select", [Ptr, C.c_int, C.c_int, C.c_int,
            C.POINTER(U64), C.POINTER(I32)])
        signature(lib, "pc_test_strategy_hash", [Ptr, op])
        signature(lib, "pc_test_sha256", [Ptr, C.c_size_t, op], None)
    return lib


def be64(value):
    return struct.pack(">Q", int(value) & MASK)


def f64(value):
    return struct.pack(">d", value)


def domain(text):
    return text.encode("ascii") + b"\0"


def public_bytes(values):
    return b"".join(be64(v) for v in values)


def rp_bytes(s):
    out = be64(s.pot)
    for p in (0, 1):
        out += b"".join(be64(getattr(s, name)[p]) for name in ("total", "bet", "stack", "reopen"))
    return out + b"".join(be64(getattr(s, name)) for name in ("high", "increment", "pending", "actor")) + f64(s.chips_per_bb)


def cert_bytes(c):
    return b"".join(f64(getattr(c, name)) for name in CERT_FLOATS) + be64(c.iterations)


def node_bytes(index, node):
    out = be64(index) + rp_bytes(node.state)
    out += b"".join(be64(getattr(node, name)) for name in ("kind", "folder", "action_count"))
    return out + b"".join(be64(node.child[a]) + be64(node.action[a]) + f64(node.payoff0[a]) for a in range(3))


def digest(data):
    return hashlib.sha256(data).digest()


def random32(state):
    x = state
    x ^= x >> 12
    x ^= (x << 25) & MASK
    x ^= x >> 27
    return x, ((x * 2685821657736338717) & MASK) >> 32


def external_rng(state):
    state, first = random32(state)
    state, second = random32(state)
    state, roll = random32(state)
    return state, ((first << 32) | second) or 1, roll % 1000


def sampled_action(node, average, row, seed):
    after, draw32 = random32(seed)
    draw = draw32 / 4294967296.0
    cumulative = 0.0
    for a in range(node.action_count):
        cumulative += average[a * HANDS + row]
        if draw < cumulative or a == node.action_count - 1:
            return int(node.action[a]), after, draw32
    raise AssertionError("empty strategy row")


def abstract_actions(s):
    actor = s[0]
    own, other = 20 + 5 * actor, 20 + 5 * (1 - actor)
    call = s[5] - s[own + 3]
    actions = [-1, 0] if call else [0]
    if (s[5] >= s[own + 4] and s[own + 3] + s[own] > s[5]
            and s[other] > 0 and s[other + 3] + s[other] > s[5]):
        target = max(s[5] + s[6], s[5] + 2 * (s[4] + min(call, s[own])) // 3)
        actions.append(min(target, s[own + 3] + s[own]))
    return actions


def next_public(before, action, full_board=None):
    """Independent HU betting ledger; Python integers avoid C overflow.

    Public terminal money is pre-payout. Showdown ranking is outside this oracle.
    Every tested edge is also checked against the separately frozen native
    engine; this ledger does not call pc_public_next to produce expectations.
    """
    s = list(before)
    actor, street, button = s[:3]
    assert actor in (0, 1) and action >= -1
    q = 1 - actor
    i, j = 20 + 5 * actor, 20 + 5 * q
    call, ceiling = s[5] - s[i + 3], s[i] + s[i + 3]
    if action == -1:
        assert call > 0
    elif action:
        assert (s[5] >= s[i + 4] and ceiling > s[5] and s[j] > 0
            and s[j] + s[j + 3] > s[5] and s[5] < action <= ceiling)
        assert action >= s[5] + s[6] or action == ceiling
    s[3] += 1
    s[9] &= ~(1 << actor)
    if action == -1:
        s[7] &= ~(1 << actor)
    else:
        kind = 3 if action else 2 if call else 1
        shift = 2 * street
        s[18 + actor] = (s[18 + actor] & ~(3 << shift)) | (max((s[18 + actor] >> shift) & 3, kind) << shift)
        target = action or s[5]
        paid = min(target - s[i + 3], s[i])
        s[i] -= paid
        s[i + 2] += paid
        s[i + 3] += paid
        s[4] += paid
        if target > s[5]:
            increase = target - s[5]
            if increase >= s[6]:
                s[6], s[10], s[11] = increase, min(4, s[10] + 1), actor
            s[5] = target
            s[9] = (1 << q) if s[j] else 0
        s[i + 4] = s[5] + s[6]
    s[8] = sum(1 << p for p in (0, 1) if s[7] & (1 << p) and s[20 + 5 * p] > 0)
    s[9] &= s[8]
    if s[7] != 3:
        s[0], s[9] = 255, 0
    else:
        live_actors = [p for p in (0, 1) if s[8] & (1 << p)]
        if len(live_actors) == 1:
            last = live_actors[0]
            s[5] = s[20 + 5 * (1 - last) + 3]
            if s[20 + 5 * last + 3] >= s[5]:
                s[9] = 0
        if s[9]:
            s[0] = q if s[9] & (1 << q) else actor
        elif street == 3 or len(live_actors) < 2:
            s[0], s[1], s[9] = 255, 3, 0
        else:
            s[1] += 1
            s[0], s[5], s[6], s[9], s[10], s[11] = 1 - button, 0, 100, s[8], 0, 255
            for p in (0, 1):
                s[20 + 5 * p + 3] = s[20 + 5 * p + 4] = 0
    s[12] = s[1] + 2 if s[1] else 0
    board = list(before[13:18] if full_board is None else full_board)
    assert len(board) == 5
    assert board[:before[12]] == before[13:13 + before[12]]
    s[13:18] = [board[k] if k < s[12] else -1 for k in range(5)]
    assert len(set(s[13:13 + s[12]])) == s[12]
    assert s[4] == s[22] + s[27]
    assert all(s[20 + 5 * p] + s[22 + 5 * p] == s[21 + 5 * p] for p in (0, 1))
    return s


def match_rcfr(expected, actual):
    assert actual.actor == (-1 if expected[0] == 255 else expected[0])
    for attr, index in (("pot", 4), ("high", 5), ("increment", 6), ("pending", 9), ("chips_per_bb", 30)):
        assert getattr(actual, attr) == expected[index], (attr, getattr(actual, attr), expected[index])
    for p in (0, 1):
        for attr, off in (("stack", 0), ("total", 2), ("bet", 3), ("reopen", 4)):
            assert getattr(actual, attr)[p] == expected[20 + 5 * p + off], (attr, p)


class Engine:
    def __init__(self, reference, fields, holes):
        self.reference = reference
        self.ptr = reference.reference_native_create((I64 * 31)(*fields), (C.c_int * 4)(*holes))
        assert self.ptr

    def values(self):
        out = (I64 * 31)()
        assert self.reference.reference_unsettled(self.ptr, out)
        return list(out)

    def close(self):
        if self.ptr:
            self.reference.reference_native_free(self.ptr)
            self.ptr = None


class ContextCase:
    def __init__(self, lib, reference, fixture, name, generation=101, seed=711,
                 policy=7, seat=None, holes=None, probe=False):
        self.lib, self.reference, self.probe = lib, reference, probe
        self.fields = list(fixture["public"])
        self.seat = self.fields[0] if seat is None else seat
        self.holes = list(itertools.chain.from_iterable(fixture["own_cards"])) if holes is None else holes
        self.own = tuple(sorted(self.holes[2 * self.seat:2 * self.seat + 2]))
        self.owner = Owner.from_buffer_copy(digest(name.encode("utf-8")))
        self.gen, self.rng, self.events = generation, U64(seed), 0
        self.ptr = lib.pc_create(self.owner, self.seat)
        assert self.ptr
        self.engine = Engine(reference, self.fields, self.holes)
        self.ok(lib.pc_begin(self.ptr, self.owner, self.gen, policy, 0))
        self.ok(lib.pc_own(self.ptr, self.owner, self.gen, *self.own))
        self.ok(lib.pc_start(self.ptr, self.owner, self.gen, C.byref(Public.of(self.fields))))
        self.prefix = digest(domain("poker-continuation-prefix-start-v1") + public_bytes(self.fields))

    def ok(self, result):
        assert result, self.lib.pc_last_error(self.ptr).decode()
        return result

    def audit(self):
        a = Audit()
        self.ok(self.lib.pc_audit(self.ptr, C.byref(a)))
        assert bytes(a.public_hash) == digest(domain("poker-continuation-public-v1") + public_bytes(self.fields))
        assert bytes(a.prefix_hash) == self.prefix
        data = domain("poker-continuation-whole-audit-v1") + bytes(a.owner)
        data += b"".join(be64(getattr(a, field)) for field in AUDIT_U64 + AUDIT_I32)
        data += bytes(a.public_hash) + bytes(a.prefix_hash) + bytes(a.anchor_hash) + bytes(a.strategy_hash)
        assert bytes(a.whole_hash) == digest(data)
        return a

    def decide(self, request=1):
        out = I32()
        decide = self.lib.probe_decide if self.probe else self.lib.pc_decide
        self.ok(decide(self.ptr, self.owner, self.gen, self.engine.ptr, 0,
            C.byref(self.rng), None, request, C.byref(out)))
        return out.value

    def act(self, action):
        before = list(self.fields)
        after = next_public(before, action)
        b, a = Public.of(before), Public.of(after)
        self.ok(self.lib.pc_public_transition(C.byref(b), action, C.byref(a)))
        self.reference.reference_native_step(self.engine.ptr, action)
        assert self.engine.values() == after
        self.ok(self.lib.pc_own(self.ptr, self.owner, self.gen, *self.own))
        self.ok(self.lib.pc_observe(self.ptr, self.owner, self.gen, C.byref(b), action, C.byref(a)))
        self.prefix = digest(domain("poker-continuation-prefix-event-v1") + self.prefix
            + be64(self.events) + public_bytes(before) + be64(action) + public_bytes(after))
        self.events += 1
        self.fields = after
        self.audit()

    def end(self, reason=1):
        self.ok(self.lib.pc_end(self.ptr, self.owner, self.gen, reason))

    def close(self):
        if self.ptr:
            self.lib.pc_free(self.ptr)
            self.ptr = None
        self.engine.close()


def counts(lib):
    out = Counts()
    lib.probe_read_counts(C.byref(out))
    return {name: getattr(out, name) for name, _ in Counts._fields_}


def export_tree(lib, ptr, reference=False):
    count_fn = lib.reference_node_count if reference else lib.pc_node_count
    node_fn = lib.reference_node if reference else lib.pc_node_info
    average_fn = lib.reference_average if reference else lib.pc_average
    total = count_fn(ptr)
    assert 1 <= total <= 4096
    nodes, averages, average_bytes = [], {}, {}
    for index in range(total):
        node = Node()
        assert node_fn(ptr, index, C.byref(node))
        nodes.append(node)
        if node.kind == 0:
            assert 1 <= node.action_count <= 3
            length = node.action_count * HANDS
            values = (C.c_double * length)()
            assert average_fn(ptr, index, values, length)
            assert not average_fn(ptr, index, values, length - 1)
            average_bytes[index] = bytes(values)
            averages[index] = list(values)
            for h in range(HANDS):
                row = [values[a * HANDS + h] for a in range(node.action_count)]
                assert all(math.isfinite(v) and 0 <= v <= 1 for v in row)
                assert abs(sum(row) - 1) <= 1e-12
    return nodes, averages, average_bytes


def tree_digest(nodes, averages):
    data = domain("poker-continuation-all-node-average-v1") + be64(HANDS) + be64(len(nodes))
    for index, node in enumerate(nodes):
        data += node_bytes(index, node)
        if node.kind == 0:
            data += b"".join(f64(v) for v in averages[index])
    return digest(data)


def anchor_digest(prefix, fields, inputs, cert, checkpoint, strategy):
    data = domain("poker-continuation-anchor-v1") + prefix + public_bytes(fields) + rp_bytes(inputs.state)
    for h in range(HANDS):
        row = inputs.hand[h]
        data += be64(row.c0) + be64(row.c1) + be64(row.rank) + f64(inputs.prior[0][h]) + f64(inputs.prior[1][h])
    return digest(data + be64(checkpoint) + cert_bytes(cert) + strategy)


def certify_fixture_prefix(reference, candidate, fixture):
    fields = fixture["public"]
    holes = list(itertools.chain.from_iterable(fixture["own_cards"]))
    engine = Engine(reference, fields, holes)
    board = fields[13:18]
    try:
        deck = holes + board
        assert len(set(deck)) == 9
        reference.poker_reset(engine.ptr, 0, fields[2], (C.c_uint32 * 2)(20000, 20000),
            (C.c_uint8 * 9)(*deck))
        state = engine.values()
        common = fields[22] - fields[23]
        actions = [common if common > 100 else 0, 0, 0, 0, 0, 0]
        if fields[5]:
            actions.append(fields[5])
        for action in actions:
            expected = next_public(state, action, board)
            b, a = Public.of(state), Public.of(expected)
            assert candidate.pc_public_transition(C.byref(b), action, C.byref(a))
            reference.reference_native_step(engine.ptr, action)
            assert engine.values() == expected
            state = expected
        assert state == fields, (fixture["id"], state, fields)
        return {"actions": actions, "decision_count": state[3],
                "scope": "fixed legal full-deal public prefix; no policy reachability claim"}
    finally:
        engine.close()


def verify_edges(reference, candidate, fixture, nodes):
    states = {0: list(fixture["public"])}
    holes = list(itertools.chain.from_iterable(fixture["own_cards"]))
    edges, allin_nodes, terminal_nodes = 0, 0, 0
    for index, node in enumerate(nodes):
        assert index in states, ("unreachable tree node", index)
        state = states[index]
        match_rcfr(state, node.state)
        if node.kind:
            assert state[0] == 255 and node.action_count == 0
            assert all(child == -1 for child in node.child)
            if state[7] == 3:
                assert node.kind == 1 and node.folder == -1
                amount = min(state[22], state[27])
                assert list(node.payoff0) == [-amount, 0, amount]
            else:
                folder = 1 if state[7] == 1 else 0
                payoff = state[27] if folder == 1 else -state[22]
                assert node.kind == 2 and node.folder == folder
                assert list(node.payoff0) == [payoff] * 3
            terminal_nodes += 1
            continue
        assert state[0] in (0, 1)
        actions = abstract_actions(state)
        assert list(node.action)[:node.action_count] == actions
        if state[8] in (1, 2):
            assert actions == [-1, 0]
            allin_nodes += 1
        for a, action in enumerate(actions):
            child = node.child[a]
            assert index < child < len(nodes) and child not in states
            expected = next_public(state, action)
            states[child] = expected
            before_c, after_c, observed = Public.of(state), Public.of(expected), Public()
            assert candidate.pc_public_next(C.byref(before_c), action,
                (I64 * 5)(*expected[13:18]), expected[12], C.byref(observed))
            assert list(observed.v) == expected
            assert candidate.pc_public_transition(C.byref(before_c), action, C.byref(after_c))
            engine = Engine(reference, state, holes)
            try:
                reference.reference_native_step(engine.ptr, action)
                assert engine.values() == expected, (fixture["id"], index, action)
            finally:
                engine.close()
            edges += 1
    assert len(states) == len(nodes) and edges == len(nodes) - 1
    return {"edges": edges, "terminal_nodes": terminal_nodes, "facing_allin_decision_nodes": allin_nodes}


def verify_rows(probe, case, nodes, averages, inputs, seeds):
    audit_before = bytes(case.audit())
    counters_before = counts(probe)
    samples = 0
    for index, node in enumerate(nodes):
        if node.kind:
            continue
        for h, hand in enumerate(inputs.hand):
            for seed in seeds:
                expected, expected_rng, draw32 = sampled_action(node, averages[index], h, seed)
                rng, action = U64(seed), I32(-2147483648)
                assert probe.pc_test_select(case.ptr, index, hand.c0, hand.c1, C.byref(rng), C.byref(action))
                assert action.value == expected and rng.value == expected_rng, (index, h, seed)
                samples += 1
        # Out-of-board and malformed private rows must fail without a draw.
        for pair in ((case.fields[13], case.fields[14]), (0, 0), (-1, 12), (12, 52)):
            rng, action = U64(91), I32(-2147483648)
            assert not probe.pc_test_select(case.ptr, index, *pair, C.byref(rng), C.byref(action))
            assert rng.value == 91 and action.value == -2147483648
    counters_after = counts(probe)
    for name in ("creates", "iterate_calls", "iterations", "certificate_calls", "external_draws"):
        assert counters_before[name] == counters_after[name]
    assert counters_after["row_draws"] - counters_before["row_draws"] == samples
    assert bytes(case.audit()) == audit_before
    return samples


def sha_vectors(probe):
    inputs = [b"", b"abc", b"a" * 55, b"a" * 56, b"a" * 63,
              bytes(range(64)), bytes(range(256)), b"canonical-receipts" * 513]
    for data in inputs:
        output = Owner()
        storage = C.create_string_buffer(data)
        probe.pc_test_sha256(storage, len(data), output)
        assert bytes(output) == digest(data)
    return len(inputs)


def frozen_oracle(reference, production, probe, fixtures, seeds):
    reports, held = [], []
    for index, fixture in enumerate(fixtures):
        prefix_report = certify_fixture_prefix(reference, production, fixture)
        fields = fixture["public"]
        ref = reference.reference_create((I64 * 31)(*fields))
        assert ref and reference.reference_status(ref) == 1, fixture["id"]
        try:
            checkpoint = reference.reference_checkpoint(ref)
            calls = reference.reference_iterate_calls(ref)
            cert, inputs = Certificate(), Inputs()
            assert reference.reference_certificate(ref, C.byref(cert))
            assert reference.reference_inputs(ref, C.byref(inputs))
            assert cert.iterations == checkpoint and cert.nashconv_bb <= 0.05
            rnodes, ravg, rbytes = export_tree(reference, ref, True)
            expected_strategy = tree_digest(rnodes, ravg)
            edge_report = verify_edges(reference, production, fixture, rnodes)
            name = f"new-native-oracle/jobA/fixture{index}/seat{fields[0]}"
            prod = ContextCase(production, reference, fixture, name, 101 + index)
            probe.probe_reset()
            test = ContextCase(probe, reference, fixture, name, 101 + index, probe=True)
            held.append((prod, test))
            action = prod.decide(1)
            assert test.decide(1) == action
            external_after, local_before, roll = external_rng(711)
            own = prod.own
            row = next(h for h, hand in enumerate(inputs.hand) if (hand.c0, hand.c1) == own)
            expected_action, local_after, local_draw = sampled_action(rnodes[0], ravg[0], row, local_before)
            assert action == expected_action and prod.rng.value == test.rng.value == external_after
            c = counts(probe)
            assert c == {"creates": 1, "iterate_calls": calls, "iterations": checkpoint,
                "certificate_calls": calls, "external_draws": 3, "row_draws": 1,
                "last_row_before": local_before, "last_row_after": local_after, "last_row_return": local_draw}
            expected_anchor = anchor_digest(prod.prefix, fields, inputs, cert, checkpoint, expected_strategy)
            for case in (prod, test):
                audit = case.audit()
                assert audit.phase == 2 and audit.anchors == audit.creates == audit.hits == 1
                assert audit.iterations == checkpoint and audit.iterate_calls == calls and audit.fallbacks == 0
                assert bytes(audit.strategy_hash) == expected_strategy and bytes(audit.anchor_hash) == expected_anchor
                observed_cert = Certificate()
                assert case.lib.pc_certificate(case.ptr, C.byref(observed_cert))
                assert cert_bytes(observed_cert) == cert_bytes(cert)
                nodes, avg, raw = export_tree(case.lib, case.ptr)
                assert len(nodes) == len(rnodes) and raw == rbytes
                assert all(node_bytes(i, n) == node_bytes(i, rnodes[i]) for i, n in enumerate(nodes))
                assert tree_digest(nodes, avg) == expected_strategy
                prior_audit, prior_rng = bytes(case.audit()), case.rng.value
                assert case.decide(1) == action
                assert bytes(case.audit()) == prior_audit and case.rng.value == prior_rng
            assert bytes(prod.audit()) == bytes(test.audit())
            samples = verify_rows(probe, test, rnodes, ravg, inputs, seeds)
            output = Owner()
            assert probe.pc_test_strategy_hash(test.ptr, output)
            assert bytes(output) == expected_strategy
            assert export_tree(probe, test.ptr)[2] == rbytes
            reports.append({"fixture": fixture["id"], "checkpoint": checkpoint,
                "certificate": {name: getattr(cert, name) for name in CERT_FLOATS},
                "nodes": len(rnodes), "decision_nodes": len(ravg),
                "averaged_entries": sum(len(v) for v in ravg.values()),
                "averaged_bytes": sum(len(v) for v in rbytes.values()),
                "row_rng_samples": samples, "first_action": action, "rng_before": 711,
                "rng_after": external_after, "local_before": local_before, "local_after": local_after,
                "strategy_sha256": expected_strategy.hex(), "anchor_sha256": expected_anchor.hex(),
                "retained_allocation_bytes": int(production.pc_allocated_bytes(prod.ptr)),
                "public_prefix": prefix_report, **edge_report})
            print(json.dumps({"fixture": fixture["id"], "nodes": len(rnodes), "row_rng_samples": samples,
                "status": "passed"}), flush=True)
        finally:
            reference.reference_free(ref)
    return reports, held


def force_action(probe, case, action, request):
    """Proof-only branch construction, never an ordinary policy action claim.

    Choose the midpoint of an already frozen row's requested action interval;
    no alternate hand or result search. The actual local RNG still advances.
    A zero-width interval is a fixed gate failure, not grounds to pick a new row.
    """
    node_index = case.audit().node
    if node_index < 0:
        # A new root has no exported row until its accepted solve. At free roots
        # action 0 is first; integer draw zero selects its positive initial row.
        assert action == 0 and case.fields[5] == 0
        draw32 = 0
    else:
        nodes, averages, _ = export_tree(probe, case.ptr)
        node = nodes[node_index]
        board = set(case.fields[13:18])
        hands = list(itertools.combinations([c for c in range(52) if c not in board], 2))
        h = hands.index(case.own)
        a = list(node.action)[:node.action_count].index(action)
        low = sum(averages[node_index][k * HANDS + h] for k in range(a))
        high = low + averages[node_index][a * HANDS + h]
        draw32 = min((1 << 32) - 1, math.floor((low + high) * 2147483648.0))
        assert low <= draw32 / 4294967296.0 < high
    probe.probe_force_local(1, draw32)
    try:
        actual = case.decide(request)
    finally:
        probe.probe_force_local(0, 0)
    assert actual == action
    return draw32


def lifecycle(reference, production, probe, control, fixtures, held):
    report = {"fault_recovery": [], "scripted_native_branches": [], "protocol_rejections": []}
    # Interleave six independent live contexts already anchored by unforced
    # native decisions. A fixed opponent chooses its largest retained action;
    # only an existing next own decision is evaluated. No new solve, outcome
    # selection, substituted hand or forced local draw enters this schedule.
    interleaved = []
    for index, (_, case) in enumerate(held):
        others = {j: bytes(pair[1].audit()) for j, pair in enumerate(held) if j != index}
        original_raw = export_tree(probe, case.ptr)[2]
        original_hash = bytes(case.audit().strategy_hash)
        before = counts(probe)
        first_action = case.audit().pending_action
        case.act(first_action)
        opponent_action, later_action = None, None
        if case.fields[0] != 255:
            assert case.fields[0] != case.seat
            opponent_action = abstract_actions(case.fields)[-1]
            case.act(opponent_action)
            if case.fields[0] != 255:
                assert case.fields[0] == case.seat and case.audit().phase == 2
                later_action = case.decide(2)
                repeat = bytes(case.audit())
                assert case.decide(2) == later_action and bytes(case.audit()) == repeat
        after = counts(probe)
        for name in ("creates", "iterate_calls", "iterations", "certificate_calls"):
            assert after[name] == before[name]
        assert after["row_draws"] - before["row_draws"] == (later_action is not None)
        assert after["external_draws"] - before["external_draws"] == 3 * (later_action is not None)
        assert bytes(case.audit().strategy_hash) == original_hash
        assert export_tree(probe, case.ptr)[2] == original_raw
        assert all(bytes(held[j][1].audit()) == value for j, value in others.items())
        interleaved.append({"fixture": fixtures[index]["id"], "first_action": first_action,
            "fixed_opponent_action": opponent_action, "later_native_action": later_action,
            "terminal_before_later_decision": case.fields[0] == 255})
    report["unforced_interleaved_contexts"] = interleaved
    # Deliberately terminal-corrupt six already-proven pending production
    # contexts. A guard failure consumes no draw or solver work.
    for index, (prod, _) in enumerate(held):
        before_rng = prod.rng.value
        out = I32()
        bad_owner = Owner.from_buffer_copy(bytes([7]) * 32)
        args = [prod.ptr, prod.owner, prod.gen, prod.engine.ptr, 0, C.byref(prod.rng), None, 1, C.byref(out)]
        if index == 0:
            args[7] = 2
            name = "new_request_before_ack"
        elif index == 1:
            prod.rng.value = 711
            before_rng = 711
            name = "pending_duplicate_requires_post_rng"
        elif index == 2:
            args[1] = bad_owner
            name = "pending_wrong_owner"
        elif index == 3:
            args[2] = prod.gen - 1
            name = "pending_stale_generation"
        elif index == 4:
            wrong = list(prod.fields)
            wrong[3] += 1
            other_engine = Engine(reference, wrong, prod.holes)
            args[3] = other_engine.ptr
            name = "pending_changed_engine_public_state"
        else:
            alternative = next(a for a in abstract_actions(prod.fields) if a != prod.audit().pending_action)
            before, after = Public.of(prod.fields), Public.of(next_public(prod.fields, alternative))
            assert not production.pc_observe(prod.ptr, prod.owner, prod.gen,
                C.byref(before), alternative, C.byref(after))
            name = "own_ack_action_mismatch"
        if index != 5:
            assert not production.pc_decide(*args)
        if index == 4:
            other_engine.close()
        assert prod.audit().phase == 5 and prod.rng.value == before_rng
        assert not production.pc_envelope(prod.ptr, prod.owner, prod.gen)
        report["protocol_rejections"].append(name)

    royal = fixtures[1]
    # Four pre-declared solver faults, then the same later eligible public
    # decision. The fixed original fallback must be a check on this royal board.
    assert external_rng(711)[2] >= 45
    for mode, status in ((1, 2), (2, 4), (3, 6), (4, 3)):
        probe.probe_reset()
        case = ContextCase(probe, reference, royal, f"native-fault-retry/{mode}", 501, probe=True)
        try:
            baseline_rng = U64(711)
            expected = control.poker_decide(case.engine.ptr, 0, C.byref(baseline_rng), 7, 0, None)
            probe.probe_fault(mode)
            action = case.decide(1)
            rejected, first = case.audit(), counts(probe)
            assert action == expected == 0 and case.rng.value == baseline_rng.value
            assert rejected.phase == 1 and rejected.anchors == rejected.hits == 0
            assert rejected.solve_status == status and rejected.fallbacks == rejected.creates == 1
            assert first["row_draws"] == 0 and first["external_draws"] == 3
            assert probe.pc_node_count(case.ptr) == 0
            probe.probe_fault(0)
            case.act(0)
            case.act(100)  # Legal but outside a would-be 1000 root menu; no anchor exists yet.
            assert case.audit().phase == 1
            second_action = case.decide(2)
            accepted, second = case.audit(), counts(probe)
            assert accepted.phase == 2 and accepted.anchors == accepted.hits == 1 and accepted.creates == 2
            assert accepted.solve_status == 1 and second["row_draws"] == 1 and second["external_draws"] == 6
            report["fault_recovery"].append({"mode": mode, "rejection_status": status,
                "first": first, "second": second, "later_action": second_action,
                "later_checkpoint": accepted.checkpoint})
        finally:
            case.close()

    for label, opponent_action in (("retained_facing_allin_active1", 1000), ("off_menu_disables_no_second_anchor", 100)):
        probe.probe_reset()
        case = ContextCase(probe, reference, royal, "native-constructed/" + label, 601, probe=True)
        try:
            forced = force_action(probe, case, 0, 1)
            base = case.audit()
            original_strategy = bytes(base.strategy_hash)
            original_bytes = export_tree(probe, case.ptr)[2]
            case.act(0)
            case.act(opponent_action)
            before = counts(probe)
            if opponent_action == 1000:
                assert case.fields[8] == 1 and case.audit().phase == 2
                node = Node()
                assert probe.pc_node_info(case.ptr, case.audit().node, C.byref(node))
                assert list(node.action)[:node.action_count] == [-1, 0]
                call_draw = force_action(probe, case, 0, 2)
            else:
                assert case.audit().phase == 3
                call_draw = None
                assert case.decide(2) == 0
            after = counts(probe)
            for key in ("creates", "iterate_calls", "iterations", "certificate_calls"):
                assert before[key] == after[key]
            assert bytes(case.audit().strategy_hash) == original_strategy
            assert export_tree(probe, case.ptr)[2] == original_bytes
            assert case.audit().anchors == 1
            if opponent_action == 1000:
                assert after["row_draws"] == before["row_draws"] + 1
                assert case.audit().hits == 2
            else:
                assert after["row_draws"] == before["row_draws"]
                assert case.audit().hits == 1 and case.audit().fallbacks == 1
            case.act(0)
            assert case.fields[0] == 255
            case.end()
            audit_closed = bytes(case.audit())
            terminal = Public.of(case.fields)
            assert probe.pc_closed_receipt(case.ptr, case.owner, case.gen, C.byref(terminal), *case.own)
            assert probe.pc_closed_receipt(case.ptr, case.owner, case.gen, C.byref(terminal), *reversed(case.own))
            assert bytes(case.audit()) == audit_closed and probe.pc_node_count(case.ptr) == 0
            report["scripted_native_branches"].append({"case": label,
                "scope": "explicit local-draw construction hook; not natural policy/path frequency",
                "forced_root_u32": forced, "forced_call_u32": call_draw,
                "counts_before_later_decision": before, "counts_after_later_decision": after})
        finally:
            case.close()

    # Terminal receipt guards require no solve: opponent folds at a fixed facing
    # root, so this context has no own policy request to acknowledge.
    for mutation in ("owner", "generation", "own", "terminal_state", "decide", "late_event"):
        case = ContextCase(production, reference, fixtures[2], "closed-guard/" + mutation, 701, seat=1)
        try:
            before_event = list(case.fields)
            case.act(-1)
            case.end()
            terminal = Public.of(case.fields)
            args = [case.ptr, case.owner, case.gen, C.byref(terminal), *case.own]
            closed = bytes(case.audit())
            assert production.pc_closed_receipt(*args)
            assert bytes(case.audit()) == closed
            if mutation == "owner":
                args[1] = Owner.from_buffer_copy(bytes([3]) * 32)
            elif mutation == "generation":
                args[2] -= 1
            elif mutation == "own":
                args[4] = 2
            elif mutation == "terminal_state":
                terminal.v[3] += 1
            elif mutation == "decide":
                out = I32()
                assert not production.pc_decide(case.ptr, case.owner, case.gen, case.engine.ptr, 0,
                    C.byref(case.rng), None, 1, C.byref(out))
            elif mutation == "late_event":
                assert not production.pc_observe(case.ptr, case.owner, case.gen,
                    C.byref(Public.of(before_event)), -1, C.byref(Public.of(case.fields)))
            if mutation not in ("decide", "late_event"):
                assert not production.pc_closed_receipt(*args)
            assert case.audit().phase == 5 and case.rng.value == 711
            report["protocol_rejections"].append("closed_" + mutation)
        finally:
            case.close()

    for mutation in ("begin_open", "own_changed", "start_repeated", "own_event_without_pending", "request_reused_after_ack"):
        # Fixed weak-pair rules row and seed91: first roll351, no raise.
        case = ContextCase(production, reference, fixtures[3], "open-guard/" + mutation, 801, seed=91, policy=1)
        try:
            if mutation == "begin_open":
                assert not production.pc_begin(case.ptr, case.owner, 802, 1, 0)
            elif mutation == "own_changed":
                assert not production.pc_own(case.ptr, case.owner, case.gen, 2, 3)
            elif mutation == "start_repeated":
                assert not production.pc_start(case.ptr, case.owner, case.gen, C.byref(Public.of(case.fields)))
            elif mutation == "own_event_without_pending":
                assert not production.pc_observe(case.ptr, case.owner, case.gen,
                    C.byref(Public.of(case.fields)), 0, C.byref(Public.of(next_public(case.fields, 0))))
            else:
                assert case.decide(1) == 0
                case.act(0)
                case.act(100)
                out = I32()
                assert not production.pc_decide(case.ptr, case.owner, case.gen, case.engine.ptr, 0,
                    C.byref(case.rng), None, 1, C.byref(out))
            assert case.audit().phase == 5
            report["protocol_rejections"].append(mutation)
        finally:
            case.close()

    # Equal public game, independent own/other holdings: public solve and anchor
    # remain byte-identical. This is one additional fixed solve, not hand search.
    reference_prod, _ = held[0]
    alternate = ContextCase(production, reference, fixtures[0], "alternate-private/fixture0", 901,
                            holes=[2, 3, 4, 5])
    try:
        alternate.decide(1)
        audit = alternate.audit()
        assert bytes(audit.anchor_hash) == bytes(reference_prod.audit().anchor_hash)
        assert bytes(audit.strategy_hash) == bytes(reference_prod.audit().strategy_hash)
        report["private_game_identity"] = {"strategy_sha256": bytes(audit.strategy_hash).hex(),
            "alternate_own": [2, 3], "alternate_other": [4, 5]}
    finally:
        alternate.close()

    # Byte-for-byte engine reconstruction leaves an open context unchanged;
    # an uncertain operation abandons it and requires a larger generation.
    case = ContextCase(production, reference, fixtures[3], "reset-rebuild-isolation", 1001, seed=91, policy=1)
    try:
        assert case.decide(1) == 0
        before = bytes(case.audit())
        rebuilt = Engine(reference, case.fields, case.holes)
        try:
            C.memmove(case.engine.ptr, rebuilt.ptr, reference.reference_native_size())
        finally:
            rebuilt.close()
        assert bytes(case.audit()) == before and case.decide(1) == 0
        case.end(2)
        assert case.audit().phase == 4
        assert production.pc_begin(case.ptr, case.owner, 1002, 1, 0)
        audit = Audit()
        assert production.pc_audit(case.ptr, C.byref(audit))
        assert audit.generation == 1002 and audit.phase == 1 and audit.pending == audit.creates == audit.anchors == 0
        report["rebuild_and_true_reset"] = "same-generation rebuild preserved exact duplicate; abandon then new generation cleared receipts"
    finally:
        case.close()
    return report


def malformed_public(candidate, fixture):
    cases = []
    normal = fixture["public"]
    for index in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 18, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30):
        for value in ((1 << 63) - 1, -(1 << 63)):
            before = list(normal)
            before[index] = value
            b, out = Public.of(before), Public()
            assert not candidate.pc_public_next(C.byref(b), 0, (I64 * 5)(*normal[13:18]), 5, C.byref(out))
            cases.append([index, value])
    for action in (-2147483648, -2, -1, 1, 99, 20001, 2147483647):
        b, out = Public.of(normal), Public()
        assert not candidate.pc_public_next(C.byref(b), action, (I64 * 5)(*normal[13:18]), 5, C.byref(out))
        cases.append(["action", action])
    for index, value in ((13, normal[14]), (8, 1), (9, 0), (22, normal[22] + 1), (30, 99)):
        before = list(normal)
        before[index] = value
        b, out = Public.of(before), Public()
        assert not candidate.pc_public_next(C.byref(b), 0, (I64 * 5)(*normal[13:18]), 5, C.byref(out))
        cases.append([index, value])
    return cases


def main():
    parser = argparse.ArgumentParser()
    for name in ("reference", "candidate", "probe", "control", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--fixtures", type=Path, default=ROOT.parent / "fixtures" / "river_roots.json")
    args = parser.parse_args()
    output = Path(args.output)
    assert not output.exists(), "proof receipts are append-only"
    report = {"schema": "new-continuation-independent-native-oracle-v1",
              "scope": "new six-fixture finite game/code proof; no historical proof recovery or playing-strength claim"}
    held = []
    try:
        path = args.fixtures.resolve()
        assert hashlib.sha256(path.read_bytes()).hexdigest() == FIXTURE_SHA
        fixture_plan = json.loads(path.read_text())
        fixtures, seeds = fixture_plan["fixtures"], fixture_plan["rng_seeds"]
        assert len(fixtures) == 6 and seeds == [91, 92, 93, 711, 881]
        assert hashlib.sha256(Path(args.control).read_bytes()).hexdigest() == CONTROL_SHA
        assert C.sizeof(Public) == 248 and C.sizeof(RPublic) == 104
        assert C.sizeof(Hand) == 8 and C.sizeof(Node) == 176 and C.sizeof(Certificate) == 96
        reference = bind_reference(args.reference)
        production = bind_candidate(args.candidate)
        probe = bind_candidate(args.probe, True)
        control = C.CDLL(args.control)
        signature(control, "poker_decide", [Ptr, C.c_int, C.POINTER(U64), C.c_int, C.c_int, Ptr], I32)
        report["sha256_vectors"] = sha_vectors(probe)
        report["malformed_public_rejections"] = malformed_public(production, fixtures[0])
        report["fixtures"], held = frozen_oracle(reference, production, probe, fixtures, seeds)
        report["lifecycle"] = lifecycle(reference, production, probe, control, fixtures, held)
        report["status"] = "passed"
    except Exception:
        report["status"] = "failed"
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        for pair in held:
            for case in pair:
                case.close()
        report["input_sha256"] = {name: hashlib.sha256(Path(getattr(args, name)).read_bytes()).hexdigest()
            for name in ("reference", "candidate", "probe", "control")}
        report["driver_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": "passed", "fixtures": 6,
        "averaged_entries": sum(f["averaged_entries"] for f in report["fixtures"]),
        "edges": sum(f["edges"] for f in report["fixtures"]),
        "row_rng_samples": sum(f["row_rng_samples"] for f in report["fixtures"])}), flush=True)


if __name__ == "__main__":
    main()
