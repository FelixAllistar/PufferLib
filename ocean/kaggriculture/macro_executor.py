"""Deterministic, observation-faithful executors for Kaggriculture macros.

The native rule core accepts one farmer command, one command per hired hand,
and a market queue per turn.  A strategic macro is therefore represented as a
short sequence of those primitive action dictionaries.  This module builds
legal sequences from a public/native snapshot; it never calls the Python
competition engine and it never invents a hidden state transition.

The executor is intentionally conservative.  It uses one unit (the farmer),
routes with the native bounds-only movement rules, and rejects a macro when
the required resources, tile, or remaining time are unavailable.  A later
optimized executor may use all hands, but it must preserve this contract.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from macro_actions import MacroAction, CROPS, ANIMALS, PRODUCTS, pass_action


SEED_COSTS = {
    "WHEAT": 10,
    "CARROT": 20,
    "TOMATO": 50,
    "STRAWBERRY": 100,
    "MELON": 80,
}
ANIMAL_COSTS = {"GOOSE": 300, "COW": 400, "SHEEP": 500}
ANIMAL_STRUCTURES = {"GOOSE": "COOP", "COW": "PASTURE", "SHEEP": "PASTURE"}
ANIMAL_PRODUCTS = {"GOOSE": "EGG", "COW": "MILK", "SHEEP": "WOOL"}
MARKET_ITEMS = (*PRODUCTS, *ANIMALS)


def _int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _positions(snapshot: dict[str, Any], player: int) -> list[tuple[int, int]]:
    farms = snapshot.get("farms") or []
    farm = farms[player] if 0 <= player < len(farms) and isinstance(farms[player], dict) else {}
    out: list[tuple[int, int]] = []
    farmer = farm.get("farmer")
    if isinstance(farmer, list) and len(farmer) >= 2:
        out.append((_int(farmer[0]), _int(farmer[1])))
    for value in farm.get("hands") or []:
        if isinstance(value, list) and len(value) >= 2:
            out.append((_int(value[0]), _int(value[1])))
    return out or [(4, 4)]


def _farm(snapshot: dict[str, Any], player: int) -> dict[str, Any]:
    farms = snapshot.get("farms") or []
    if 0 <= player < len(farms) and isinstance(farms[player], dict):
        return farms[player]
    return {}


def _private(snapshot: dict[str, Any], player: int) -> dict[str, Any]:
    privates = snapshot.get("privates") or []
    if 0 <= player < len(privates) and isinstance(privates[player], dict):
        return privates[player]
    return {}


def _tiles(snapshot: dict[str, Any], player: int) -> Iterable[tuple[int, int, Any]]:
    for y, row in enumerate(_farm(snapshot, player).get("tiles") or []):
        if not isinstance(row, list):
            continue
        for x, tile in enumerate(row):
            yield x, y, tile


def _empty_tiles(snapshot: dict[str, Any], player: int) -> list[tuple[int, int]]:
    return [(x, y) for x, y, tile in _tiles(snapshot, player) if tile is None]


def _shed(private: dict[str, Any]) -> dict[str, int]:
    raw = private.get("shed") or {}
    return {item: max(0, _int(raw.get(item))) for item in MARKET_ITEMS}


def _inventory(private: dict[str, Any], unit: int = 0) -> dict[str, int]:
    inventories = private.get("inventories") or []
    raw = inventories[unit] if 0 <= unit < len(inventories) and isinstance(inventories[unit], dict) else {}
    return {item: max(0, _int(raw.get(item))) for item in MARKET_ITEMS}


def _money(farm: dict[str, Any]) -> int:
    return _int(farm.get("money"))


def _shed_total(shed: dict[str, int]) -> int:
    return sum(max(0, value) for value in shed.values())


def _farm_hands(snapshot: dict[str, Any], player: int) -> int:
    return len(_farm(snapshot, player).get("hands") or [])


def _market(kind: str, item: str | None = None, quantity: int = 0) -> list[Any]:
    order: list[Any] = [kind]
    if item is not None:
        order.append(item)
    if quantity > 0:
        order.append(int(quantity))
    return order


def _primitive(
    snapshot: dict[str, Any], player: int, unit_index: int,
    command: list[Any] | None = None, market: list[list[Any]] | None = None,
) -> dict[str, Any]:
    """Build one structured action with PASS for every non-selected unit."""

    hands = [["PASS"] for _ in range(_farm_hands(snapshot, player))]
    command = command or ["PASS"]
    if unit_index == 0:
        farmer = command
    else:
        farmer = ["PASS"]
        if 0 < unit_index <= len(hands):
            hands[unit_index - 1] = command
    return {"farmer": farmer, "hands": hands, "market": list(market or [])}


def _route(
    snapshot: dict[str, Any], player: int, unit_index: int,
    start: tuple[int, int], target: tuple[int, int],
) -> tuple[list[dict[str, Any]], tuple[int, int]]:
    """Return shortest deterministic bounds-only movement actions."""

    x, y = start
    tx, ty = target
    actions: list[dict[str, Any]] = []
    while x != tx:
        command = ["EAST"] if x < tx else ["WEST"]
        x += 1 if x < tx else -1
        actions.append(_primitive(snapshot, player, unit_index, command))
    while y != ty:
        command = ["SOUTH"] if y < ty else ["NORTH"]
        y += 1 if y < ty else -1
        actions.append(_primitive(snapshot, player, unit_index, command))
    return actions, (x, y)


def _with_first_market(actions: list[dict[str, Any]], order: list[Any]) -> list[dict[str, Any]]:
    if not actions:
        return actions
    actions[0]["market"] = [order]
    return actions


def _finish(
    action_id: str, kind: str, item: str, quantity: int,
    sequence: list[dict[str, Any]], *, note: str,
    remaining_turns: int,
) -> MacroAction | None:
    if not sequence or len(sequence) > max(0, remaining_turns):
        return None
    return MacroAction(
        action_id, kind, item, quantity,
        action=sequence[0], sequence=tuple(sequence), note=note,
    )


def _choose_access(snapshot: dict[str, Any], player: int) -> tuple[int, int]:
    board = len(_farm(snapshot, player).get("tiles") or []) or 10
    half = board // 2
    # All four squares are valid shed access positions; the first is stable.
    return max(0, half - 1), max(0, half - 1)


def plant_macro(
    snapshot: dict[str, Any], player: int, crop: str, quantity: int,
    *, episode_steps: int = 720,
) -> MacroAction | None:
    crop = str(crop).upper()
    quantity = int(quantity)
    if crop not in CROPS or quantity <= 0:
        return None
    targets = _empty_tiles(snapshot, player)[:quantity]
    if len(targets) < quantity:
        return None
    farm = _farm(snapshot, player)
    private = _private(snapshot, player)
    shed = _shed(private)
    seeds = _int((private.get("seeds") or {}).get(crop))
    need = max(0, quantity - seeds)
    cost = need * SEED_COSTS[crop]
    if _money(farm) < cost:
        return None
    remaining = max(0, episode_steps - _int(snapshot.get("step")))
    position = _positions(snapshot, player)[0]
    sequence: list[dict[str, Any]] = []
    market = _market("BUY_SEED", crop, need) if need else []
    for index, target in enumerate(targets):
        route, position = _route(snapshot, player, 0, position, target)
        if not sequence and route:
            sequence.extend(route)
            if market:
                sequence[0]["market"] = [market]
        elif not sequence and market:
            sequence.append(_primitive(snapshot, player, 0, market=[market]))
        sequence.append(_primitive(snapshot, player, 0, ["PLANT", crop]))
    return _finish(
        f"PLANT:{crop}:{quantity}", "PLANT", crop, quantity, sequence,
        note="route farmer and plant the requested crop count",
        remaining_turns=remaining,
    )


def animal_macro(
    snapshot: dict[str, Any], player: int, animal: str, quantity: int,
    *, episode_steps: int = 720, shed_capacity: int = 100,
) -> MacroAction | None:
    animal = str(animal).upper()
    quantity = int(quantity)
    if animal not in ANIMALS or quantity <= 0:
        return None
    structure = ANIMAL_STRUCTURES[animal]
    targets: list[tuple[int, int, Any]] = []
    for x, y, tile in _tiles(snapshot, player):
        if isinstance(tile, dict) and str(tile.get("kind", "")).upper() == structure and not tile.get("animal"):
            targets.append((x, y, tile))
    targets.extend((x, y, tile) for x, y, tile in _tiles(snapshot, player) if tile is None)
    targets = targets[:quantity]
    if len(targets) < quantity:
        return None
    farm = _farm(snapshot, player)
    private = _private(snapshot, player)
    shed = _shed(private)
    unit_inventory = _inventory(private, 0)
    held = unit_inventory.get(animal, 0)
    missing = max(0, quantity - held - shed.get(animal, 0))
    purchase_cost = missing * ANIMAL_COSTS[animal]
    shed_room = max(0, int(shed_capacity) - _shed_total(shed))
    if _money(farm) < purchase_cost or missing > shed_room:
        return None
    remaining = max(0, episode_steps - _int(snapshot.get("step")))
    position = _positions(snapshot, player)[0]
    sequence: list[dict[str, Any]] = []
    purchase = _market("BUY_ANIMAL", animal, missing) if missing else []
    access = _choose_access(snapshot, player)
    # Purchase first, while the farmer begins its trip to the shed.  The native
    # market is processed independently of the unit command in that turn.
    route, position = _route(snapshot, player, 0, position, access)
    if route:
        sequence.extend(route)
        if purchase:
            sequence[0]["market"] = [purchase]
    elif purchase:
        sequence.append(_primitive(snapshot, player, 0, market=[purchase]))
    # Existing unit inventory is available immediately; purchased/shed animals
    # must be picked up at the shed before placement.
    need_pickup = quantity - held
    if need_pickup > 0:
        sequence.append(_primitive(snapshot, player, 0, ["PICKUP", animal, need_pickup]))
        held += need_pickup
    for x, y, tile in targets:
        route, position = _route(snapshot, player, 0, position, (x, y))
        sequence.extend(route)
        if tile is None:
            sequence.append(_primitive(
                snapshot, player, 0,
                ["BUILD_COOP" if structure == "COOP" else "BUILD_PASTURE"],
            ))
        sequence.append(_primitive(snapshot, player, 0, ["PLACE", animal]))
        held -= 1
    return _finish(
        f"BUILD_ANIMAL:{animal}:{quantity}", "BUILD_ANIMAL", animal, quantity,
        sequence, note="buy/pick up, build if needed, and place animals",
        remaining_turns=remaining,
    )


def harvest_macro(
    snapshot: dict[str, Any], player: int, *, episode_steps: int = 720,
) -> MacroAction | None:
    targets = [
        (x, y) for x, y, tile in _tiles(snapshot, player)
        if isinstance(tile, dict) and _int(tile.get("yield_units")) > 0
    ]
    if not targets:
        return None
    remaining = max(0, episode_steps - _int(snapshot.get("step")))
    position = _positions(snapshot, player)[0]
    sequence: list[dict[str, Any]] = []
    for target in targets:
        route, position = _route(snapshot, player, 0, position, target)
        sequence.extend(route)
        sequence.append(_primitive(snapshot, player, 0, ["HARVEST"]))
    return _finish(
        "HARVEST_READY", "HARVEST", "", len(targets), sequence,
        note="route to every currently harvestable tile",
        remaining_turns=remaining,
    )


def maintenance_macro(
    snapshot: dict[str, Any], player: int, *, episode_steps: int = 720,
) -> MacroAction | None:
    """Service currently due plants/animals with a deterministic farmer route."""

    tasks: list[tuple[tuple[int, int], list[str]]] = []
    feed_needed = 0
    for x, y, tile in _tiles(snapshot, player):
        if not isinstance(tile, dict):
            continue
        kind = str(tile.get("kind", "")).upper()
        ops: list[str] = []
        if kind == "PLANT" and not bool(tile.get("watered_today", False)):
            ops.append("WATER")
        if kind in {"COOP", "PASTURE"} and tile.get("animal"):
            if not bool(tile.get("fed_today", False)):
                ops.append("FEED")
                feed_needed += 1
            if kind == "PASTURE" and not bool(tile.get("cared_today", False)):
                ops.append("CARE")
            if bool(tile.get("fertilizer_available", False)):
                ops.append("COLLECT_FERTILIZER")
        if ops:
            tasks.append(((x, y), ops))
    if not tasks:
        return None
    private = _private(snapshot, player)
    unit_inventory = _inventory(private, 0)
    shed = _shed(private)
    available_wheat = unit_inventory.get("WHEAT", 0) + shed.get("WHEAT", 0)
    if feed_needed > available_wheat:
        return None
    remaining = max(0, episode_steps - _int(snapshot.get("step")))
    position = _positions(snapshot, player)[0]
    sequence: list[dict[str, Any]] = []
    pickup = max(0, feed_needed - unit_inventory.get("WHEAT", 0))
    if pickup:
        access = _choose_access(snapshot, player)
        route, position = _route(snapshot, player, 0, position, access)
        sequence.extend(route)
        sequence.append(_primitive(snapshot, player, 0, ["PICKUP", "WHEAT", pickup]))
    for target, ops in tasks:
        route, position = _route(snapshot, player, 0, position, target)
        sequence.extend(route)
        for op in ops:
            sequence.append(_primitive(snapshot, player, 0, [op]))
    return _finish(
        "MAINTAIN_DUE", "MAINTAIN", "", len(tasks), sequence,
        note="service all currently due plant/animal tasks with available feed",
        remaining_turns=remaining,
    )


def sell_all_macro(snapshot: dict[str, Any], player: int) -> MacroAction | None:
    shed = _shed(_private(snapshot, player))
    orders = [_market("SELL", item, amount) for item, amount in shed.items() if amount > 0]
    if not orders:
        return None
    # The native rule limit is 32; Kaggriculture exposes nine products, so all
    # product liquidation orders fit in one primitive turn.
    action = _primitive(snapshot, player, 0, market=orders)
    return MacroAction(
        "SELL_ALL", "SELL_ALL", "", sum(shed.values()),
        action=action, sequence=(action,), note="liquidate every held market product",
    )


def strategic_actions(
    snapshot: dict[str, Any], player: int, *, episode_steps: int = 720,
    shed_capacity: int = 100,
) -> list[MacroAction]:
    """Return feasible multi-turn strategic candidates for a snapshot."""

    candidates: list[MacroAction] = []
    for crop in CROPS:
        for quantity in (5, 10, 25):
            candidate = plant_macro(snapshot, player, crop, quantity, episode_steps=episode_steps)
            if candidate is not None:
                candidates.append(candidate)
    for animal in ANIMALS:
        for quantity in (1, 3):
            candidate = animal_macro(
                snapshot, player, animal, quantity,
                episode_steps=episode_steps, shed_capacity=shed_capacity,
            )
            if candidate is not None:
                candidates.append(candidate)
    for candidate in (
        harvest_macro(snapshot, player, episode_steps=episode_steps),
        maintenance_macro(snapshot, player, episode_steps=episode_steps),
        sell_all_macro(snapshot, player),
    ):
        if candidate is not None:
            candidates.append(candidate)
    return candidates
