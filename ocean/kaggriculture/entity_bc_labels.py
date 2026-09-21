"""Conservative primitive -> mode 2/executor 1 inverse projection.

This is NOT recovery of a demonstrator's latent macro decisions. We only label
one identifiable strategic intent, then ask the production executor which
quantity bins reproduce its market orders and kinds of strategic work. Worker
assignment/pathing may differ. Ambiguous quantity/target heads are unsupervised;
conflicting intents are skipped. Every observation still trains the RNN/critic.
Routine PLACE/PICKUP/FEED/HARVEST are automatic in executor 1, NOT an order to buy
another animal. A HOLD label requires exact primitive executor agreement.
"""
from __future__ import annotations

import collections

import numpy as np

import replay_native as native

QUANTITIES = (1, 2, 4, 8, 12, 20, 32, 64)
TARGETS = (0, 1, 2, 4, 8)
HEADS, OBS, MASK = 47, 1424, 1978
POLICY_VERSION = 5


def markets(action, *, include_feed=False):
    result = collections.Counter()
    for order in action.market[:action.market_count]:
        if not include_feed and (order.op, order.item) == (1, 0):
            continue  # Mandatory wheat purchasing is executor-owned.
        # HIRE/BUY_LAND have no quantity argument in Kaggle JSON. The parser
        # stores n=0, but the engine executes each order once regardless of n.
        quantity = 1 if order.op in (4, 5) else order.n
        if quantity > 0:
            result[(order.op, order.item)] += quantity
    return result


def strategy(action):
    result = set()
    for cmd in [action.farmer, *action.hands[:action.hand_count]]:
        if cmd.op in (7, 10, 11, 12, 13):  # plant/fertilize/build/dig
            result.add((cmd.op, cmd.arg if cmd.op == 7 else -1))
    return result


def primitive_equal(a, b):
    def unit(c):
        return (c.op, c.arg if c.op in (5, 7, 14) else -1,
                max(1, c.n) if c.op in (5, 14) else 1)
    return (a.hand_count == b.hand_count
            and unit(a.farmer) == unit(b.farmer)
            and all(unit(x) == unit(y) for x, y in zip(
                a.hands[:a.hand_count], b.hands[:b.hand_count]))
            and [(o.op, o.item, 1 if o.op in (4, 5) else o.n) for o in a.market[:a.market_count]]
            == [(o.op, o.item, 1 if o.op in (4, 5) else o.n) for o in b.market[:b.market_count]])


def candidates(action):
    work = strategy(action)
    orders = markets(action)
    keys = list(orders)
    if keys:
        if all(op == 3 and 0 <= item < 9 for op, item in keys):
            return [10 + keys[0][1]] if len(keys) == 1 else [19]
        if len(keys) != 1:
            return []
        op, item = keys[0]
        if op == 0 and 0 <= item < 5:
            return [1 + item] if (7, item) in work else [20 + item]
        if op == 2 and 9 <= item < 12:
            return [6 + item - 9] if any(w[0] in (11, 12) for w in work) else [25 + item - 9]
        return {(4, -1): [28], (5, -1): [9], (1, 8): [33]}.get((op, item), [])
    crops = {arg for op, arg in work if op == 7}
    if len(crops) == 1 and all(op in (7, 13) for op, _ in work):
        return [1 + next(iter(crops))]
    # A pasture alone does not identify cow vs sheep; do not guess.
    return {frozenset({(11, -1)}): [6], frozenset({(13, -1)}): [35],
            frozenset({(10, -1)}): [36]}.get(frozenset(work), [])


def project(action, mask, decode):
    """Return sparse labels, canonical history heads, and diagnostic reason.

    decode(heads) must call the same native executor used for live PPO. We do
    not infer a target from a moving worker: all five targets are tested and
    only uniquely determined parameters receive gradients.
    """
    label = np.full(HEADS, -1, np.float32)
    history = np.zeros(HEADS, np.float32)
    if not strategy(action) and not markets(action):
        if primitive_equal(action, decode(history)):
            label[0] = 0
            return label, history, "exact_hold"
        return label, history, "routine_unidentified"
    ids = candidates(action)
    if not ids:
        return label, history, "conflicting_or_unsupported_intent"
    matches = []
    for macro in ids:
        if not mask[macro]:
            continue
        for q in range(8):
            for target in range(5):
                heads = np.zeros(HEADS, np.float32)
                heads[:3] = macro, q, target
                actual = decode(heads)
                if markets(actual) == markets(action) and strategy(actual) == strategy(action):
                    matches.append((macro, q, target))
    if not matches:
        return label, history, "executor_cannot_reproduce_strategy"
    history[:3] = matches[0]  # documented canonical latent-history approximation
    for head in range(3):
        values = {m[head] for m in matches}
        if len(values) == 1:
            label[head] = next(iter(values))
    if label[0] < 0:
        label[:] = -1
        return label, history, "ambiguous_macro"
    return label, history, "projected_strategy"
