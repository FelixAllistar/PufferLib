"""Controller-independent, lossless action records plus multi-intent annotations.

The raw action is authoritative. These labels record OBSERVED work/ordered
trades, not inferred hidden plans (moving east does not reveal a destination,
and building pasture does not identify cow versus sheep). All simultaneous
requests are retained. Existing single-intent 2/1 consumes only its separate
.bc file and never sees this sidecar until a new trainer explicitly opts in.
"""
from __future__ import annotations

import copy

GROWTH = {"PLANT", "BUILD_COOP", "BUILD_PASTURE", "DIG", "FERTILIZE"}
CHORES = {"WATER", "FEED", "CARE", "HARVEST", "COLLECT_FERTILIZER", "PICKUP", "PLACE", "DROP"}
MOVEMENT = {"NORTH", "SOUTH", "EAST", "WEST"}


def annotate(action, positions, *, episode_id, player, step, single_label, reason):
    """Keep all choices; never silently map multiple choices to one false label."""
    groups = {}
    chores, movement, unresolved = [], [], []
    commands = [action.get("farmer", ["PASS"]), *action.get("hands", [])]
    for worker, command in enumerate(commands):
        if not isinstance(command, list) or not command:
            unresolved.append({"worker": worker, "command": copy.deepcopy(command)})
            continue
        op = command[0]
        position = list(map(int, positions[worker])) if worker < len(positions) else None
        record = dict(worker=worker, position=position, command=copy.deepcopy(command))
        if op in GROWTH:
            item = command[1] if op == "PLANT" and len(command) > 1 else None
            key = (op, item)
            group = groups.setdefault(key, dict(operation=op, item=item, count=0, workers=[], positions=[]))
            group["count"] += 1; group["workers"].append(worker); group["positions"].append(position)
        elif op in CHORES:
            chores.append(record)
        elif op in MOVEMENT:
            movement.append(record)
        elif op != "PASS":
            unresolved.append(record)
    market = []
    for index, order in enumerate(action.get("market", [])):
        record = dict(index=index, command=copy.deepcopy(order))
        if isinstance(order, list) and order:
            record["operation"] = order[0]
            record["item"] = order[1] if len(order) > 1 else None
            record["quantity"] = 1 if order[0] in ("HIRE", "BUY_LAND") else (order[2] if len(order) > 2 else None)
        market.append(record)
    strategic_count = len(groups) + len(market)
    return dict(format="kaggriculture_multi_intent_v1", episode_id=str(episode_id),
                player=player, step=step, primitive_action=copy.deepcopy(action),
                production_requests=list(groups.values()), market_queue=market,
                chore_trace=chores, movement_trace=movement, unresolved_trace=unresolved,
                strategic_request_count=strategic_count,
                single_2_1=dict(heads=[int(x) for x in single_label[:3]], reason=reason),
                supervision="observed commands; latent plans and movement goals are unknown")
