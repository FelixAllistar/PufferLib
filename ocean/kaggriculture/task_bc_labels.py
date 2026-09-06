#!/usr/bin/env python3
"""Behavior-equivalent replay labels for Kaggriculture ``macro_mode=3``.

Mode 3 does not imitate primitive walking. Each live policy head requests a
mechanical task and the native executor assigns/routes a worker. Replay work
commands therefore map directly to task tokens, while a bounded lookahead
propagates the first eventual work command backward across that worker's
movement. PASS remains IDLE so the converter cannot invent work merely because
the same worker does something much later.

The result is a prioritized task multiset, not a mapping from task head to a
particular farmer/hand. Non-IDLE tokens are packed toward head zero because the
executor itself selects workers and lower task heads are conflict priority.
"""

from __future__ import annotations

import collections
from typing import Any, Sequence

try:
    from macro_bc_labels import RUNTIME
except ImportError:  # pragma: no cover - package invocation
    from ocean.kaggriculture.macro_bc_labels import RUNTIME


MOVEMENT = frozenset(("NORTH", "SOUTH", "EAST", "WEST"))
TRACEABLE = MOVEMENT | frozenset(("PICKUP",))


def _get(obj: Any, key: str, default: Any = None) -> Any:
    if obj is None:
        return default
    try:
        return obj.get(key, default)
    except AttributeError:
        return getattr(obj, key, default)


def _op(command: Any) -> str:
    if not isinstance(command, (list, tuple)) or not command:
        return "INVALID"
    return str(command[0]).upper()


def _commands(action: Any) -> list[Any]:
    if not isinstance(action, dict):
        return []
    hands = _get(action, "hands", ())
    if not isinstance(hands, (list, tuple)):
        hands = ()
    return [_get(action, "farmer", ["PASS"]), *hands]


def _farm(observation: dict[str, Any]) -> dict[str, Any]:
    player = int(_get(observation, "player", 0))
    farms = _get(observation, "farms", ())
    return farms[player]


def _positions(observation: dict[str, Any]) -> list[tuple[int, int]]:
    farm = _farm(observation)
    raw = [_get(farm, "farmer", (4, 4)), *_get(farm, "hands", ())]
    result = []
    for position in raw:
        if not isinstance(position, (list, tuple)) or len(position) < 2:
            result.append((4, 4))
        else:
            result.append((int(position[0]), int(position[1])))
    return result


def _tile(observation: dict[str, Any], x: int, y: int) -> Any:
    tiles = _get(_farm(observation), "tiles", ())
    if y < 0 or y >= len(tiles) or x < 0 or x >= len(tiles[y]):
        return None
    return tiles[y][x]


def _kind(tile: Any) -> str:
    if isinstance(tile, str):
        return tile.upper()
    return str(_get(tile, "kind", "")).upper()


def _quadrant_index(x: int, y: int) -> int:
    return (1 if x >= 5 else 0) + (2 if y >= 5 else 0)


def direct_task_token(
    observation: dict[str, Any], unit: int, command: Any,
) -> int | None:
    """Translate one actual work command to one mode-3 task token."""

    op = _op(command)
    simple = {
        "PASS": RUNTIME.TASK_IDLE,
        "WATER": RUNTIME.TASK_WATER,
        "FERTILIZE": RUNTIME.TASK_FERTILIZE,
        "FEED": RUNTIME.TASK_FEED,
        "CARE": RUNTIME.TASK_CARE,
        "COLLECT_FERTILIZER": RUNTIME.TASK_COLLECT_FERTILIZER,
        "DROP": RUNTIME.TASK_DROP,
    }
    if op in simple:
        return int(simple[op])
    positions = _positions(observation)
    if unit < 0 or unit >= len(positions):
        return None
    x, y = positions[unit]
    quadrant = _quadrant_index(x, y)
    if op == "HARVEST":
        kind = _kind(_tile(observation, x, y))
        if kind == "PLANT":
            return int(RUNTIME.TASK_HARVEST_CROP)
        if kind in ("COOP", "PASTURE"):
            return int(RUNTIME.TASK_HARVEST_ANIMAL)
        return None
    if op == "DIG":
        return int(RUNTIME.TASK_CLEAR_BASE) + quadrant
    if op == "PLANT" and isinstance(command, (list, tuple)) \
            and len(command) >= 2:
        crop = str(command[1]).upper()
        if crop in RUNTIME.CROPS:
            return (int(RUNTIME.TASK_PLANT_BASE)
                    + 4 * RUNTIME.CROPS.index(crop) + quadrant)
        return None
    if op == "BUILD_COOP":
        return int(RUNTIME.TASK_BUILD_COOP_BASE) + quadrant
    if op == "BUILD_PASTURE":
        return int(RUNTIME.TASK_BUILD_PASTURE_BASE) + quadrant
    if op == "PLACE" and isinstance(command, (list, tuple)) \
            and len(command) >= 2:
        animal = str(command[1]).upper()
        if animal in RUNTIME.ANIMALS:
            return int(RUNTIME.TASK_ADD_GOOSE) + RUNTIME.ANIMALS.index(animal)
    return None


def _trace_task(
    observations: Sequence[dict[str, Any]], actions: Sequence[dict[str, Any]],
    turn: int, unit: int, lookahead: int,
) -> tuple[int, str]:
    commands = _commands(actions[turn])
    current = commands[unit] if unit < len(commands) else ["PASS"]
    op = _op(current)
    direct = direct_task_token(observations[turn], unit, current)
    if direct is not None and op not in TRACEABLE:
        return direct, "direct"
    if op not in TRACEABLE:
        return int(RUNTIME.TASK_IDLE), "idle"

    # A hand is one-day labor. Never attribute tomorrow's newly hired worker's
    # task to today's worker merely because it reused the same array index.
    start_day = int(_get(observations[turn], "day", turn // 24))
    stop = min(len(actions), turn + max(1, int(lookahead)) + 1)
    for future in range(turn + 1, stop):
        if unit > 0 and int(_get(observations[future], "day", future // 24)) \
                != start_day:
            break
        future_commands = _commands(actions[future])
        if unit >= len(future_commands) or unit >= len(_positions(observations[future])):
            break
        command = future_commands[unit]
        future_op = _op(command)
        token = direct_task_token(observations[future], unit, command)
        if token is not None and future_op not in TRACEABLE \
                and future_op != "PASS":
            return token, "routed"
        if future_op not in MOVEMENT and future_op not in ("PASS", "PICKUP"):
            break
    return int(RUNTIME.TASK_IDLE), "unresolved"


def trajectory_task_tokens(
    observations: Sequence[dict[str, Any]], actions: Sequence[dict[str, Any]],
    *, lookahead: int = 32, unit_heads: int = 17,
) -> tuple[list[list[int]], collections.Counter[str]]:
    """Return one packed task-token list for every replay decision row."""

    if len(observations) != len(actions):
        raise ValueError("task observations/actions length mismatch")
    audit: collections.Counter[str] = collections.Counter()
    result: list[list[int]] = []
    for turn, observation in enumerate(observations):
        live = min(len(_positions(observation)), int(unit_heads))
        tokens = []
        for unit in range(live):
            token, reason = _trace_task(
                observations, actions, turn, unit, lookahead,
            )
            tokens.append(int(token))
            audit[f"task_{reason}"] += 1
            audit[f"task_token_{int(token)}"] += 1
        # Heads are task slots, not worker identities. Pack useful intents in
        # source-worker order, then canonical IDLE slots.
        active = [token for token in tokens if token != RUNTIME.TASK_IDLE]
        result.append(active + [int(RUNTIME.TASK_IDLE)] * (live - len(active)))
    return result, audit


__all__ = ["direct_task_token", "trajectory_task_tokens"]
