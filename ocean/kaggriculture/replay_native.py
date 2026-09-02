"""Small ctypes bridge shared by offline Kaggriculture replay-state tools."""

from __future__ import annotations

import ctypes
import json


KG_MAX_HANDS = 240
KG_MAX_MARKET_ORDERS = 32


class CUnitAction(ctypes.Structure):
    _fields_ = [("op", ctypes.c_int), ("arg", ctypes.c_int), ("n", ctypes.c_int)]


class CMarketOrder(ctypes.Structure):
    _fields_ = [("op", ctypes.c_int), ("item", ctypes.c_int), ("n", ctypes.c_int)]


class CAction(ctypes.Structure):
    _fields_ = [
        ("farmer", CUnitAction),
        ("hands", CUnitAction * KG_MAX_HANDS),
        ("hand_count", ctypes.c_int),
        ("market", CMarketOrder * KG_MAX_MARKET_ORDERS),
        ("market_count", ctypes.c_int),
    ]


class CConfig(ctypes.Structure):
    _fields_ = [
        ("episode_steps", ctypes.c_int),
        ("board_size", ctypes.c_int),
        ("starting_money", ctypes.c_int),
        ("max_market_orders_per_turn", ctypes.c_int),
        ("turns_per_day", ctypes.c_int),
        ("shed_capacity", ctypes.c_int),
        ("weed_spawn_chance", ctypes.c_double),
        ("town_shop_unlock_interval", ctypes.c_int),
        ("town_shop_sell_interval", ctypes.c_int),
        ("town_center_sell_interval", ctypes.c_int),
        ("farm_hand_cost_mult", ctypes.c_int),
        ("seed", ctypes.c_uint64),
    ]


def load_core(path):
    lib = ctypes.CDLL(str(path))
    lib.kg_config_default.argtypes = [ctypes.POINTER(CConfig)]
    lib.kg_config_default.restype = None
    lib.kg_create.argtypes = [ctypes.POINTER(CConfig)]
    lib.kg_create.restype = ctypes.c_void_p
    lib.kg_destroy.argtypes = [ctypes.c_void_p]
    lib.kg_done.argtypes = [ctypes.c_void_p]
    lib.kg_done.restype = ctypes.c_int
    lib.kg_state_step.argtypes = [ctypes.c_void_p]
    lib.kg_state_step.restype = ctypes.c_int
    lib.kg_player_money.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.kg_player_money.restype = ctypes.c_int
    if hasattr(lib, "kg_rule_action_ex"):
        lib.kg_rule_action_ex.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(CAction),
        ]
        lib.kg_rule_action_ex.restype = None
    lib.kg_rule_action.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(CAction)]
    lib.kg_rule_action.restype = None
    lib.kg_step.argtypes = [ctypes.c_void_p, ctypes.POINTER(CAction)]
    lib.kg_snapshot_json.argtypes = [ctypes.c_void_p]
    lib.kg_snapshot_json.restype = ctypes.c_void_p
    lib.kg_free_string.argtypes = [ctypes.c_void_p]
    lib.kg_state_serialized_size.restype = ctypes.c_size_t
    lib.kg_state_serialization_version.restype = ctypes.c_uint32
    lib.kg_state_serialize.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    lib.kg_state_serialize.restype = ctypes.c_int
    lib.kg_state_deserialize.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    lib.kg_state_deserialize.restype = ctypes.c_int
    if hasattr(lib, "kg_policy_observation"):
        lib.kg_policy_observation.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t,
        ]
        lib.kg_policy_observation.restype = None
    if hasattr(lib, "kg_policy_action_mask"):
        lib.kg_policy_action_mask.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t,
        ]
        lib.kg_policy_action_mask.restype = None
    if hasattr(lib, "kg_policy_hand_count"):
        lib.kg_policy_hand_count.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.kg_policy_hand_count.restype = ctypes.c_int
    return lib


UNIT_OPS = {
    name: index for index, name in enumerate((
        "PASS", "NORTH", "SOUTH", "EAST", "WEST", "PICKUP", "DROP",
        "PLANT", "WATER", "HARVEST", "FERTILIZE", "BUILD_COOP",
        "BUILD_PASTURE", "DIG", "PLACE", "FEED", "COLLECT_FERTILIZER", "CARE",
    ))
}
MARKET_OPS = {
    name: index for index, name in enumerate(
        ("BUY_SEED", "BUY_PRODUCT", "BUY_ANIMAL", "SELL", "HIRE", "BUY_LAND")
    )
}
CROPS = {name: i for i, name in enumerate(("WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON"))}
ITEMS = {
    name: i for i, name in enumerate((
        "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON", "EGG", "MILK",
        "WOOL", "FERTILIZER", "GOOSE", "COW", "SHEEP",
    ))
}
ANIMALS = {name: 9 + i for i, name in enumerate(("GOOSE", "COW", "SHEEP"))}


def unit_action(action) -> CUnitAction:
    if not isinstance(action, list) or not action:
        return CUnitAction(0, -1, 0)
    name = action[0]
    arg = -1
    n = 0
    if name == "PLANT":
        arg = CROPS.get(action[1], -1) if len(action) > 1 else -1
    elif name in ("PICKUP", "PLACE"):
        arg = ITEMS.get(action[1], -1) if len(action) > 1 else -1
        n = int(action[2]) if len(action) > 2 else 1
    return CUnitAction(UNIT_OPS.get(name, 0), arg, n)


def market_order(order) -> CMarketOrder:
    if not isinstance(order, list) or not order:
        return CMarketOrder(-1, -1, 0)
    name = order[0]
    item = -1
    n = 0
    if name == "BUY_SEED":
        item = CROPS.get(order[1], -1) if len(order) > 1 else -1
        n = int(order[2]) if len(order) > 2 else 0
    elif name in ("BUY_PRODUCT", "SELL"):
        item = ITEMS.get(order[1], -1) if len(order) > 1 else -1
        n = int(order[2]) if len(order) > 2 else 0
    elif name == "BUY_ANIMAL":
        item = ANIMALS.get(order[1], -1) if len(order) > 1 else -1
        n = int(order[2]) if len(order) > 2 else 0
    return CMarketOrder(MARKET_OPS.get(name, -1), item, n)


def c_action(action) -> CAction:
    out = CAction()
    out.farmer = unit_action(action.get("farmer", ["PASS"]))
    hands = action.get("hands", [])
    out.hand_count = min(len(hands) if isinstance(hands, list) else 0, KG_MAX_HANDS)
    for i in range(out.hand_count):
        out.hands[i] = unit_action(hands[i])
    market = action.get("market", [])
    out.market_count = min(len(market) if isinstance(market, list) else 0, KG_MAX_MARKET_ORDERS)
    for i in range(out.market_count):
        out.market[i] = market_order(market[i])
    return out


def clone_action(action) -> CAction | dict:
    """Copy either a ctypes action or a structured JSON action."""

    if isinstance(action, CAction):
        return CAction.from_buffer_copy(bytes(action))
    if isinstance(action, dict):
        return json.loads(json.dumps(action))
    raise TypeError(f"unsupported action type: {type(action)!r}")


def canonical_replay_frame(frame):
    obs0 = frame[0]["observation"]
    value = {
        "step": obs0["step"],
        "day": obs0["day"],
        "hour": obs0["hour"],
        "done": all(agent.get("status") == "DONE" for agent in frame),
        "farms": obs0["farms"],
        "privates": [agent["observation"]["private"] for agent in frame],
        "market": obs0["market"],
        "town": obs0["town"],
    }
    return json.loads(json.dumps(value))


def c_snapshot(lib, state):
    pointer = lib.kg_snapshot_json(state)
    if not pointer:
        raise RuntimeError("kg_snapshot_json returned NULL")
    try:
        return json.loads(ctypes.string_at(pointer).decode("utf-8"))
    finally:
        lib.kg_free_string(pointer)


def first_difference(left, right, path="$", limit=8):
    if type(left) is not type(right):
        return [(path, left, right)]
    if isinstance(left, dict):
        out = []
        for key in sorted(set(left) | set(right)):
            if key not in left or key not in right:
                out.append((f"{path}.{key}", left.get(key), right.get(key)))
            else:
                out.extend(first_difference(left[key], right[key], f"{path}.{key}", limit))
            if len(out) >= limit:
                return out[:limit]
        return out
    if isinstance(left, list):
        out = []
        if len(left) != len(right):
            out.append((f"{path}.length", len(left), len(right)))
        for i, (a, b) in enumerate(zip(left, right)):
            out.extend(first_difference(a, b, f"{path}[{i}]", limit))
            if len(out) >= limit:
                return out[:limit]
        return out
    return [] if left == right else [(path, left, right)]


def replay_config(replay) -> CConfig:
    raw = replay["configuration"]
    if raw.get("marketParams"):
        raise ValueError("native replay parity currently supports default marketParams only")
    return CConfig(
        episode_steps=int(raw.get("episodeSteps", 720)),
        board_size=int(raw.get("boardSize", 10)),
        starting_money=int(raw.get("startingMoney", 3000)),
        max_market_orders_per_turn=int(raw.get("maxMarketOrdersPerTurn", 10)),
        turns_per_day=int(raw.get("turnsPerDay", 24)),
        shed_capacity=int(raw.get("shedCapacity", 100)),
        weed_spawn_chance=float(raw.get("weedSpawnChance", 0.005)),
        town_shop_unlock_interval=int(raw.get("townShopUnlockInterval", 3)),
        town_shop_sell_interval=int(raw.get("townShopSellInterval", 4)),
        town_center_sell_interval=int(raw.get("townCenterSellInterval", 24)),
        farm_hand_cost_mult=int(raw.get("farmHandCostMult", 1)),
        seed=int((replay.get("info") or {}).get("seed", raw.get("seed", 0))),
    )
