"""Sparse observed-strategy supervision for 2/2; NOT hidden-plan recovery.

Orders retain order, exact quantity, and intent. Unsupported orders suppress
the remaining queue's labels, never turn into a false STOP. Movement goals
remain unknown. Raw traces are preserved independently by the dataset builder.
"""
from __future__ import annotations

import collections
import itertools
import numpy as np
from entity_bc_labels import HEADS, MASK, primitive_equal

SIZES = [44] * 17 + [2, 21, 100] * 10
OFFSETS = np.cumsum([0, *SIZES[:-1]])
WORK = {11: 6, 12: 7, 13: 8, 10: 9}


def market_command(order):
    op, item = order.op, order.item
    if op == 0 and 0 <= item < 5:
        return item
    if op == 1 and item in (0, 8):
        return 5 + (item == 8)
    if op == 2 and 9 <= item < 12:
        return 7 + item - 9
    if op == 3 and 0 <= item < 9:
        return 10 + item
    return {4: 19, 5: 20}.get(op)


def market_conflict(result, mask):
    for slot in range(10):
        h = 17 + 3*slot
        for node in range(3):
            k = h+node
            if result[k] >= 0 and not mask[OFFSETS[k]+int(result[k])]:
                return slot, node
    return 10, 0


def observed_groups(action, positions, facts, board_size, report):
    groups = collections.Counter()
    moving = False
    for worker, cmd in enumerate([action.farmer, *action.hands[:action.hand_count]]):
        moving |= cmd.op in (1, 2, 3, 4)
        intent = 1 + cmd.arg if cmd.op == 7 and 0 <= cmd.arg < 5 else WORK.get(cmd.op)
        if facts is not None and worker < len(facts):
            crop, tile_changed, deposited, _ = facts[worker]
            if intent is not None and not tile_changed:
                report['ineffective_production_omitted'] += 1
                continue
            if cmd.op == 9 and crop >= 0 and tile_changed:  # crop HARVEST (not animal upkeep)
                intent = 11 + int(crop)
                report['crop_harvest_labeled'] += 1
            if deposited and cmd.op in (6, 14):  # DROP or PLACE into shed
                intent = 28 if cmd.op == 6 else 16 + cmd.arg
                report['delivery_labeled'] += 1
        if intent is None:
            continue
        if worker >= len(positions):
            report['unknown_position'] += 1
            continue
        x, y = positions[worker]
        # Native quadrant bits: NW=1, NE=2, SW=4, SE=8.
        region = 1 + int(x >= board_size // 2) + 2 * int(y >= board_size // 2)
        groups[intent, region] += 1
    return groups, moving


def encode_groups(groups):
    heads = np.zeros(HEADS, np.float32)
    for slot, ((intent, region), count) in enumerate(sorted(groups.items())):
        heads[3*slot:3*slot+3] = (intent,count-1,region)
    return heads


def compact_groups(groups, decode, preview_facts, positions, board_size):
    """Only collapse an overflowing request set if native effects agree.

    Let automatic chores cover ripe harvests, or merge identical intents from
    several regions. Check operation/crop/region counts, not worker identity.
    This is bounded teacher-label canonicalization, not policy-time search.
    """
    harvests = [key for key in sorted(groups) if 11 <= key[0] < 16]
    candidates = []
    for count in range(1,min(len(harvests),4)+1):
        for omit in itertools.combinations(harvests,count):
            candidate = collections.Counter({k:v for k,v in groups.items() if k not in omit})
            if len(candidate) <= 5: candidates.append(candidate)
            if len(candidates) >= 32: break
        if len(candidates) >= 32: break
    for intent in sorted({i for i,_ in groups}):
        matching = [k for k in groups if k[0] == intent]
        if len(matching) <= 1: continue
        candidate = collections.Counter({k:v for k,v in groups.items() if k not in matching})
        candidate[intent,0] = sum(groups[k] for k in matching)
        if len(candidate) <= 5 and candidate[intent,0] <= 44: candidates.append(candidate)
    for candidate in candidates:
        heads = encode_groups(candidate)
        for manual in (0,1):
            heads[16] = manual
            preview = decode(heads)
            actual,_ = observed_groups(preview,positions,preview_facts(preview),board_size,collections.Counter())
            if actual == groups:
                return candidate, manual
    return groups, 0


def project(action, mask, decode, positions, teacher_mask, board_size=10, facts=None, preview_facts=None):
    result = np.full(HEADS, -1, np.float32)
    history = np.zeros(HEADS, np.float32)
    report = collections.Counter()
    groups, moving = observed_groups(action,positions,facts,board_size,report)
    wanted_groups = groups.copy()
    report['observed_production_groups'] = len(groups)
    if len(groups) > 5 and preview_facts is not None:
        groups, manual = compact_groups(groups,decode,preview_facts,positions,board_size)
        history[16] = manual
        report['production_compacted'] = int(len(groups) <= 5)
    production_ok = len(groups) <= 5 and all(n <= 44 for n in groups.values())
    if production_ok and groups:
        # Stable canonical ordering removes arbitrary worker-order permutations.
        for slot, ((intent, region), count) in enumerate(sorted(groups.items())):
            h = 3 * slot
            history[h:h+3] = result[h:h+3] = (intent, count-1, region)
        # No STOP label if other workers could be travelling toward another goal.
        if not moving and len(groups) < 5:
            result[3*len(groups)] = 0
    elif not production_ok:
        report['production_overflow'] += 1
    elif not moving:
        # Only teach AUTO when the actual automatic worker actions agree.
        preview = decode(history)
        a_market, b_market = action.market_count, preview.market_count
        action.market_count = preview.market_count = 0
        same = primitive_equal(action, preview)
        action.market_count, preview.market_count = a_market, b_market
        if same:
            result[0] = 0
    report['movement_goal_unknown'] = int(moving)

    if facts is not None and production_ok:
        # Harvesting can be a strategic decision to WAIT, too. Keep automatic
        # ripe harvest unless it adds a crop harvest the expert didn't request.
        # Only current standing tiles/actions are compared; no future plans.
        preview = decode(history)
        actual,_ = observed_groups(preview,positions,preview_facts(preview) if preview_facts else facts,
                                   board_size,collections.Counter())
        extra = history[16] == 1 or any(n > wanted_groups[k] for k,n in actual.items() if 11 <= k[0] < 16)
        history[16] = int(extra)
        # An unresolved movement-only row supplies no trustworthy production
        # plan, so don't teach its crop-harvest preference either.
        if groups or not moving:
            result[16] = int(extra)
        report['manual_crop_harvest'] = int(extra)

    # Start with explicit-only feed while recovering the ordered market queue.
    history[15] = 1
    complete = action.market_count <= 10
    for slot, order in enumerate(action.market[:min(10, action.market_count)]):
        command = market_command(order)
        quantity = 1 if order.op in (4, 5) else order.n
        if command is None or not 1 <= quantity <= 100:
            report['unsupported_market_order'] += 1
            complete = False
            break
        h = 17 + 3*slot
        history[h:h+3] = (1, command, quantity-1)
        result[h:h+2] = (1, command)
        if command < 19:
            result[h+2] = quantity-1
    if complete and action.market_count < 10:
        result[17+3*action.market_count] = 0

    # Reject an entire dependent production suffix or market suffix when a
    # requested head is masked. Do not supervise children under a false prefix.
    teacher_mask(history, mask)
    for slot in range(5):
        h = 3*slot
        if any(result[k] >= 0 and not mask[OFFSETS[k]+int(result[k])] for k in range(h,h+3)):
            result[h:15] = -1
            history[h:15] = 0
            report['production_mask_conflict'] += 1
            break
    teacher_mask(history, mask)
    # The market is chosen after work, so it cannot reach backward into the
    # automatic worker allocator. A narrow feed-stock reservation choice lets
    # the policy sell wheat without chores preemptively carrying it away.
    first = market_conflict(result, mask)
    if first[0] < 10:
        history[15] = 2
        teacher_mask(history, mask)
        if market_conflict(result, mask) <= first:
            history[15] = 1
            teacher_mask(history, mask)
    first = market_conflict(result, mask)
    if first[0] < 10:
        slot, node = first
        command = int(history[18+3*slot])
        report[f'market_invalid_node_{node}_command_{command}'] += 1
        report[f'market_conflict_slot_{slot}'] += 1
    for slot in range(10):
        h = 17+3*slot
        if any(result[k] >= 0 and not mask[OFFSETS[k]+int(result[k])] for k in range(h,h+3)):
            result[h:] = -1
            history[h:] = 0
            report['market_mask_conflict'] += 1
            complete = False
            break
    teacher_mask(history, mask)
    report['market_queue_exact_label'] = int(complete)
    if complete:
        explicit_feed = int(history[15])
        history[15] = 0
        automatic = decode(history)
        # Prefer automatic feed whenever it preserves the expert's queue.
        # Otherwise supervise the narrowly scoped procurement opt-out.
        teacher_mask(history, mask)
        compatible = automatic.market_count == action.market_count and market_conflict(result,mask)[0] == 10
        history[15] = result[15] = 0 if compatible else explicit_feed
        report['automatic_feed_compatible'] = int(history[15] == 0)
        report['shed_feed_reserved'] = int(history[15] == 2)
    elif history[15] == 2:
        # A valid prefix may depend on the reservation even when a later
        # unsupported order makes the complete queue unrepresentable.
        result[15] = 2
    teacher_mask(history, mask)
    report['production_labeled'] = int(any(result[h] > 0 for h in range(0,15,3)))
    report['actor_row'] = int(np.any(result >= 0))
    preview = decode(history)
    wanted = collections.Counter((c.op, c.arg if c.op == 7 else -1)
                                for c in [action.farmer,*action.hands[:action.hand_count]]
                                if c.op in (7,10,11,12,13))
    executed = collections.Counter((c.op, c.arg if c.op == 7 else -1)
                                  for c in [preview.farmer,*preview.hands[:preview.hand_count]]
                                  if c.op in (7,10,11,12,13))
    report['production_execution_exact'] = int(bool(wanted) and wanted == executed)
    report['production_observed_row'] = int(bool(wanted))
    report['primitive_exact'] = int(primitive_equal(action, preview))
    return result, history, report
